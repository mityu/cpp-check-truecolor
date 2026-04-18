#include <charconv>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <poll.h>
#include <termios.h>
#include <unistd.h>

constexpr static std::string toHex(const std::string_view s);
constexpr static std::string fromHex(const std::string_view s);
void printResponse(const std::string_view s);

class Options {
public:
    struct List {
        int timeout = 300;
        int teardownTimeout = 30;
        bool help = false;
        std::string_view programName;
    };
    std::optional<std::string> parse(std::span<const std::string_view> args);
    const List& values() const noexcept;
    operator const List&() const noexcept;

public:
    static Options& get();
    static const List& getDefault();
    static void showHelp();

private:
    List values_;
};

class ProtocolBase {
public:
    virtual constexpr std::string buildQuery() const = 0;
    virtual void parseResponse(std::string_view s) = 0;
    [[nodiscard]] virtual bool doesSupportTruecolor() const = 0;
};

// True color support checker with XTGETTCAP.
// https://invisible-island.net/xterm/ctlseqs/ctlseqs.html
class Xtgettcap final : public ProtocolBase {
public:
    constexpr std::string buildQuery() const override;
    void parseResponse(std::string_view s) override;
    [[nodiscard]] bool doesSupportTruecolor() const override;

private:
    struct {
        bool setrgbb = false;
        bool setrgbf = false;
    } caps;
    bool supportTruecolor = false;
};

// True color support checker with DECRQSS SGR.
// https://vt100.net/docs/vt510-rm/DECRQSS.html
class DECRQSS final : public ProtocolBase {
public:
    constexpr std::string buildQuery() const override;
    void parseResponse(std::string_view s) override;
    [[nodiscard]] bool doesSupportTruecolor() const override;

private:
    static constexpr struct {
        char r, g, b;
    } color = {'3', '5', '7'};
    bool supportTruecolor = false;
};

class CreateRawModeScope {
public:
    CreateRawModeScope() noexcept {
        tcgetattr(STDIN_FILENO, &oldt);

        struct termios newt(oldt);

        // Invalidate echo and canonical mode.
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    }

    ~CreateRawModeScope() noexcept { tcsetattr(STDIN_FILENO, TCSANOW, &oldt); }

private:
    struct termios oldt;
};

class TruecolorChecker {
public:
    bool check() const;
    ~TruecolorChecker() noexcept;
};

constexpr std::string toHex(const std::string_view s) {
    constexpr auto hexDigits = "0123456789abcdef";
    std::string output;
    output.resize(s.size() * 2, '\0');
    for (size_t i = 0; i < s.size(); ++i) {
        output[2 * i] = hexDigits[s[i] >> 4];
        output[2 * i + 1] = hexDigits[s[i] & 0xf];
    }
    return output;
}

constexpr std::string fromHex(const std::string_view s) {
    if ((s.size() & 0x1) == 1) {
        std::cerr << "Invalid hex string: " << s << std::endl;
        std::exit(1);
    }
    const auto size = s.size() / 2;
    std::string output;
    output.resize(size, '\0');

    auto iter = s.cbegin();
    for (int i = 0; i < size; ++i, iter += 2) {
        int value = 0;
        std::from_chars(iter, iter + 2, value, 16);
        output[i] = static_cast<char>(value);
    }
    return output;
}

constexpr std::string Xtgettcap::buildQuery() const {
    constexpr std::array<std::string_view, 4> caps = {"Tc", "RGB", "setrgbf", "setrgbb"};
    std::string query;
    query += "\eP+q";
    for (auto iter = caps.cbegin(), end = caps.cend();;) {
        query += toHex(*iter);
        if (++iter == end) {
            break;
        }
        query += ";";
    }
    query += "\e\\";
    return query;
}

void Xtgettcap::parseResponse(std::string_view s) {
    if (constexpr std::string_view prefix = "1+r"; s.starts_with(prefix)) {
        s.remove_prefix(prefix.size());
    } else {
        return;
    }

    if (const auto p = s.find("="); p != std::string_view::npos) {
        s = s.substr(0, p);
    }
    const auto cap = fromHex(s);

    if (cap == "Tc" || cap == "RGB") {
        supportTruecolor = true;
        return;
    }

    if (cap == "setrgbb") {
        caps.setrgbb = true;
    } else if (cap == "setrgbf") {
        caps.setrgbf = true;
    }

    if (caps.setrgbb && caps.setrgbf) {
        supportTruecolor = true;
    }
}

bool Xtgettcap::doesSupportTruecolor() const {
    return supportTruecolor;
}

constexpr std::string DECRQSS::buildQuery() const {
    constexpr char reset[] = "\e[0m";
    constexpr char decrqss[] = "\eP$qm\e\\";
    std::string setbg;
    std::string query;

    // TODO: Construct `setbg` using std::format() when it come to be constexpr and many
    // compiler supports it.
    setbg = setbg + "\e[48;2;" + color.r + ';' + color.g + ';' + color.b + 'm';
    query = query + reset + setbg + decrqss + reset;
    return query;
}

void DECRQSS::parseResponse(std::string_view s) {
    constexpr std::string_view prefix = "1$r";
    constexpr std::string_view suffix = "m";
    if (!(s.starts_with(prefix) && s.ends_with(suffix))) {
        return;
    }

    s.remove_prefix(prefix.size());
    s.remove_suffix(suffix.size());

    // We send DECRQSS sequence as follows.
    //  1. Reset all attributes
    //  2. Set background color (SGR)
    // Therefore, the response body should be one of these.
    //  - 48:2:{rgb info}
    //  - 0; 48:2:{rgb info}
    // The variance is why some terminal include an answer for the "Reset all
    // attributes" query in response and the other do not.

    if (const std::string_view sv = "0;"; s.starts_with(sv)) {
        s.remove_prefix(sv.size());
    }
    if (s.find(";") != std::string_view::npos) {
        return;
    }

    // Here is the start of check for the answer of the SGR request.
    if (const std::string_view sv = "48:2:"; s.starts_with(sv)) {
        s.remove_prefix(sv.size());
    } else {
        // Invalid response.
        return;
    }

    constexpr auto split = [](std::string_view s, const std::string_view sep) {
        std::vector<std::string_view> vec;
        for (;;) {
            auto p = s.find(sep);
            if (p == std::string_view::npos) {
                vec.push_back(s);
                break;
            }
            vec.push_back(s.substr(0, p));
            s.remove_prefix(p + sep.size());
        }
        return vec;
    };

    // The rest should be `(:|1:)?r:g:b`.  Return if the rest does not match this.
    auto r = split(s, ":");
    if (r.size() == 4) {
        if (r[0] == "" || r[0] == "1") {
            r.erase(r.begin());
        }
    }
    if (r.size() != 3) {
        return;
    }

    // Lastly, check if the reported colors are same as what we requested.
    const auto eqColor = [](const std::string_view s, char c) {
        return s.size() == 1 && s.front() == c;
    };
    if (!(eqColor(r[0], color.r) && eqColor(r[1], color.g) && eqColor(r[2], color.b))) {
        return;
    }

    supportTruecolor = true;
}

bool DECRQSS::doesSupportTruecolor() const {
    return supportTruecolor;
}

bool TruecolorChecker::check() const {
    constexpr size_t ReadBufSize = 50;
    constexpr std::string_view DCS = "\eP";
    constexpr std::string_view ST = "\e\\";

    const auto timeout = Options::get().values().timeout;
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    char readbuf[ReadBufSize] = {};
    std::string buffer{""};

    std::array<std::variant<Xtgettcap, DECRQSS>, 2> protocols = {
        Xtgettcap(),
        DECRQSS(),
    };

    for (const auto& p : protocols) {
        std::cout << std::visit([](const auto& v) { return v.buildQuery(); }, p);
    }
    std::cout << std::flush;

    while (poll(&pfd, 1, timeout) > 0) {
        const size_t readbytes = read(STDIN_FILENO, readbuf, ReadBufSize);
        if (readbytes == 0) {
            break;
        }
        buffer += std::string_view(readbuf, readbytes);
        // printResponse(buffer);
        if (!buffer.starts_with(DCS)) {
            const auto p = buffer.find(DCS);
            if (p != std::string::npos) {
                buffer.erase(0, p);
            }
        }

        const auto p = buffer.find(ST);
        if (p == std::string::npos) {
            continue;
        }
        const std::string response = buffer.substr(0 + DCS.size(), p - ST.size());
        buffer.erase(0, p);

        for (auto& protocol : protocols) {
            std::visit([&response](auto& v) { v.parseResponse(response); }, protocol);
            const auto supportTruecolor =
                std::visit([](const auto& v) { return v.doesSupportTruecolor(); }, protocol);
            if (supportTruecolor) {
                return true;
            }
        }
    }
    return false;
}

TruecolorChecker::~TruecolorChecker() noexcept {
    constexpr size_t BufSize = 30;
    const auto timeout = Options::get().values().teardownTimeout;
    struct pollfd pfd = {STDIN_FILENO, POLLIN, 0};
    char buf[BufSize] = {};
    while (poll(&pfd, 1, timeout) > 0 && read(STDIN_FILENO, buf, BufSize) > 0)
        ;
}

Options& Options::get() {
    static Options options;
    return options;
}

const Options::List& Options::getDefault() {
    static List values;
    return values;
}

void Options::showHelp() {
    const auto values = Options::get().values();
    const auto defaultValues = Options::getDefault();
    std::cout
        << "Usage: " << values.programName << " [<options> ...]\n"
        << '\n'
        << "<options>:\n"
        << "  -h|--help\n"
        << "      Show this help\n"
        << "  --timeout <ms>\n"
        << "      Timeout for waiting response from terminal.\n"
        << "      Default is " << defaultValues.timeout << "(ms).\n"
        << "      You can set smaller value to finish truecolor detection in shorter\n"
        << "      time but it increases the risk that this program misses to recieve\n"
        << "      response from terminal and fails to detect truecolor support even if\n"
        << "      your terminal actually supports it.  You shouln't set too small value\n"
        << "      when response from terminal can late e.g. when logged into some server\n"
        << "      over SSH.\n"
        << "      This program exists immediately when it finds that your terminal has\n"
        << "      support for truecolor.  Therefore setting bigger value to this shouldn't\n"
        << "      problematic when your terminal supports truecolor; bigger value will\n"
        << "      problematic when your terminal lacks truecolor support.  In that case,\n"
        << "      this program may wait for responses from terminal forever until timeout\n"
        << "      and you will have to wait for a bit long time.\n"
        << "  --teardown-timeout <ms>\n"
        << "      Timeout for waiting remaining response from terminal.\n"
        << "      Default is " << defaultValues.teardownTimeout << "(ms).\n"
        << "      After this timeout, responses from terminal will appear on screen.\n"
        << "      You can set smaller value but it increases the risk that weird string\n"
        << "      appear on screen.  Or when you can set bigger value, it reduces the\n"
        << "      risk that weird text floods on screen but it takes much longer time\n"
        << "      from this program to exit.\n"
        << std::endl;
}

std::optional<std::string> Options::parse(std::span<const std::string_view> args) {
    constexpr std::string_view errprefix = "\e[1m\e[31merror:\e[0m ";
    std::stringstream err;
    auto iter = args.begin();
    values_.programName = *iter++;
    for (; iter != args.end(); ++iter) {
        if (*iter == "--help") {
            values_.help = true;
        } else if (*iter == "--timeout" || *iter == "--teardown-timeout") {
            const auto flag = *iter;
            if (++iter == args.end()) {
                err << errprefix << "No argument after '" << flag << "' flag.\n";
                break;
            }

            int value = 0;
            const auto [ptr, ec] = std::from_chars(iter->cbegin(), iter->cend(), value);
            if (ec == std::errc{}) {
                if (ptr != iter->cend()) {
                    constexpr std::string_view premsg = "Invalid character in number: ";
                    std::string indent(premsg.size() + (ptr - iter->cbegin()), ' ');
                    err << errprefix << "Invalid argument after '" << flag << "' flag.\n"
                        << premsg << *iter << '\n'
                        << indent << '^' << '\n';
                }
            } else if (ec == std::errc::invalid_argument) {
                err << errprefix << "Invalid argument after '" << flag << "' flag.\n"
                    << "A number in milliseconds is required: " << *iter << '\n';
            } else if (ec == std::errc::result_out_of_range) {
                err << errprefix << "Invalid argument after '" << flag << "' flag.\n"
                    << "Given number overflows int: " << *iter << '\n';
            }

            if (flag == "--timeout") {
                values_.timeout = value;
            } else {
                values_.teardownTimeout = value;
            }
        } else {
            std::stringstream ss;
            err << errprefix << "Unknown argument: " << *iter << '\n';
        }
    }

    if (auto s = err.str(); !s.empty()) {
        return s.erase(s.find_last_not_of(" \t\n") + 1);
    }
    return std::nullopt;
}

const Options::List& Options::values() const noexcept {
    return values_;
}

Options::operator const List&() const noexcept {
    return values();
}

void printResponse(const std::string_view s) {
    for (const auto& c : s) {
        switch (c) {
        case '\033':
            std::cout << "\\e";
            break;
        default:
            std::cout << c;
            break;
        }
    }
    std::cout << std::endl;
}

template <typename T>
T runInRawMode(std::function<T()> f) {
    CreateRawModeScope _rawModeScope;
    return f();
}

int main(int argv, char *argc[]) {
    const std::vector<std::string_view> args(argc, argc + argv);
    if (const auto err = Options::get().parse(args); err) {
        std::cerr << *err << std::endl;
        return -1;
    }
    if (Options::get().values().help) {
        Options::showHelp();
        return 0;
    }

    return runInRawMode<int>([]() {
        TruecolorChecker checker;
        return checker.check() ? 0 : 1;
    });
}
