// deno-lint-ignore no-import-prefix
import { assert, assertEquals, assertMatch } from "jsr:@std/assert@^1.0.19";
import { setTimeout } from "node:timers/promises";

const defaultExecutable = await (async () => {
  const exe = Deno.env.get("CHECK_TRUECOLOR_EXECUTABLE") ??
    new URL("../check-truecolor", import.meta.url).pathname;
  await Deno.stat(exe); // This throws an error when file not exist.
  return exe;
})();

type RunOptions = {
  /** Input text to give child process via stdin. */
  stdin?: string;

  /** Timeout to kill child process when it does not return. */
  timeout?: number;

  /** Path to the executable */
  executable?: string;
};

type RunResultOk = {
  result: "ok";
  output: Deno.CommandOutput;
};
type RunResultTimeout = {
  result: "timeout";
  params: {
    timeout: number;
  };
};
type RunResult = RunResultOk | RunResultTimeout;

async function run(
  runOpts: RunOptions,
  cmdOpts: Pick<Deno.CommandOptions, "args" | "cwd" | "clearEnv" | "env"> = {},
): Promise<RunResult> {
  const opts = {
    stdin: "",
    timeout: 10 * 1000,
    executable: defaultExecutable,
    ...runOpts,
  };
  const controller = new AbortController();
  const command = new Deno.Command(opts.executable, {
    signal: controller.signal,
    stdin: "piped",
    stdout: "piped",
    stderr: "piped",
    ...cmdOpts,
  });
  const child = command.spawn();
  const writer = child.stdin.getWriter();
  await writer.ready.then(() =>
    writer.write(new TextEncoder().encode(opts.stdin))
  );

  const proc = child.output().then((output) => ({
    result: "ok" as const,
    output,
  }))
    .finally(
      () => {
        writer.ready.then(() => writer.close());
      },
    );
  const timer = setTimeout(opts.timeout, { timeout: opts.timeout }, {
    signal: controller.signal,
  }).then((params) => ({ result: "timeout" as const, params }));

  return Promise.race([proc, timer]).finally(() => controller.abort());
}

function isResultOk(result: RunResult): result is RunResultOk {
  return result.result === "ok";
}

function assertResultOk(result: RunResult): asserts result is RunResultOk {
  assert(isResultOk(result), JSON.stringify(result));
}

function toHex(s: string): string {
  return s
    .split("")
    .map((c) => c.charCodeAt(0).toString(16).padStart(2, "0"))
    .join("");
}

const response = {
  xtgettc: {
    Tc: `\x1bP1+r${toHex("Tc")}\x1b\\`,
    RGB: `\x1bP1+r${toHex("RGB")}\x1b\\`,
    setrgbf: `\x1bP1+r${toHex("setrgbf")}\x1b\\`,
    setrgbb: `\x1bP1+r${toHex("setrgbb")}\x1b\\`,
    RGB24: `\x1bP1+r${toHex("RGB")}=${toHex("24")}\x1b\\`,
    RGB888: `\x1bP1+r${toHex("RGB")}=${toHex("8/8/8")}\x1b\\`,
  },
  noXtgettc: {
    Tc: `\x1bP0+r${toHex("Tc")}\x1b\\`,
    RGB: `\x1bP0+r${toHex("RGB")}\x1b\\`,
    setrgbf: `\x1bP0+r${toHex("setrgbf")}\x1b\\`,
    setrgbb: `\x1bP0+r${toHex("setrgbb")}\x1b\\`,
  },
  decrqss: {
    valid1: "\x1bP1$r48:2:3:5:7m\x1b\\",
    valid2: "\x1bP1$r48:2::3:5:7m\x1b\\",
    valid3: "\x1bP1$r48:2:1:3:5:7m\x1b\\",
    extraParam1: "\x1bP1$r0;48:2:3:5:7m\x1b\\",
    extraParam2: "\x1bP1$r0;48:2::3:5:7m\x1b\\",
    extraParam3: "\x1bP1$r0;48:2:1:3:5:7m\x1b\\",
  },
  noDecrqss: {
    noValid: "\x1bP0$r48:2:3:5:7m\x1b\\",
    wrongColor: "\x1bP1$r48:2:111:111:111m\x1b\\",
    tooMuchReport: "\x1bP1$r0;48:2:3:5:7;4m\x1b\\",
  },
} as const;

async function testTruecolorDetection(
  t: Deno.TestContext,
  code: number,
  stdin: string,
) {
  await t.step(stdin.replaceAll("\x1b", "\\e"), async () => {
    const r = await run({ stdin });
    assertResultOk(r);
    assertEquals(r.output.code, code);
    assertEquals(new TextDecoder().decode(r.output.stderr), "");
  });
}

Deno.test("truecolor support detection test", async (t) => {
  await t.step("it should detect truecolor support", async (t) => {
    const runTest = (t: Deno.TestContext, stdin: string) =>
      testTruecolorDetection(t, 0, stdin);

    await t.step("response string only", async (t) => {
      await runTest(t, response.xtgettc.Tc);
      await runTest(t, response.xtgettc.RGB);
      await runTest(t, response.xtgettc.setrgbf + response.xtgettc.setrgbb);
      await runTest(t, response.xtgettc.RGB24);
      await runTest(t, response.xtgettc.RGB888);
      await runTest(t, response.xtgettc.RGB24 + response.xtgettc.Tc);
      await runTest(t, response.decrqss.valid1);
      await runTest(t, response.decrqss.valid2);
      await runTest(t, response.decrqss.valid3);
      await runTest(t, response.decrqss.extraParam1);
      await runTest(t, response.decrqss.extraParam2);
      await runTest(t, response.decrqss.extraParam3);
    });

    await t.step("with extra string", async (t) => {
      await runTest(t, `abc${response.xtgettc.Tc}xyz`);
      await runTest(t, `\x1b[1m${response.xtgettc.Tc}\x1b[1m`);
    });

    await t.step("mixed response of support and nosupport", async (t) => {
      await runTest(t, response.noXtgettc.RGB + response.xtgettc.Tc);
    });
  });

  await t.step("it should NOT detect truecolor support", async (t) => {
    const runTest = (t: Deno.TestContext, stdin: string) =>
      testTruecolorDetection(t, 1, stdin);

    await t.step("No valid response", async (t) => {
      await runTest(t, "");
      await runTest(t, "\x1b[1m");
      await runTest(t, "xyz");
    });

    await t.step("Either of xtgettc setrgb", async (t) => {
      await runTest(t, response.xtgettc.setrgbf);
      await runTest(t, response.xtgettc.setrgbb);
    });

    await t.step("Have response but doesn't support truecolor", async (t) => {
      const res = response.noXtgettc;
      await runTest(t, res.Tc);
      await runTest(t, res.RGB);
      await runTest(t, res.setrgbf);
      await runTest(t, res.setrgbf);
      await runTest(t, res.setrgbf + res.setrgbb);
      await runTest(t, res.Tc + res.RGB + res.setrgbf + res.setrgbb);
      await runTest(t, response.noDecrqss.noValid);
    });

    await t.step("Wrong color report by decrqss", async (t) => {
      await runTest(t, response.noDecrqss.wrongColor);
    });

    await t.step("tooMuchReport", async (t) => {
      await runTest(t, response.noDecrqss.tooMuchReport);
    });
  });

  await t.step("it should consume extra text", async () => {
    const timeout = {
      readStdin: 100,
      teardown: 1000,
      entire: 10 * 1000,
    } as const;
    const controller = new AbortController();
    const command = new Deno.Command(defaultExecutable, {
      signal: controller.signal,
      args: [
        "--timeout",
        timeout.readStdin.toString(),
        "--teardown-timeout",
        timeout.teardown.toString(),
      ],
      stdin: "piped",
      stdout: "null",
      stderr: "piped",
    });
    const child = command.spawn();
    const writer = child.stdin.getWriter();

    let sentStdin = "notyet";
    const shutdownTimer = setTimeout(timeout.entire, undefined, {
      signal: controller.signal,
    }).finally(() => {
      throw Error("timeout!");
    });
    const stdinTimer = setTimeout(timeout.readStdin, undefined, {
      signal: controller.signal,
    }).then(() => {
      writer.write(new TextEncoder().encode("abcxyz"));
      sentStdin = "sent.";
    });
    const proc = child.output().finally(() =>
      writer.ready.then(() => writer.close())
    );

    const r = await proc;
    assertEquals(sentStdin, "sent.");
    assertEquals(new TextDecoder().decode(r.stderr), "");

    // FIXME: Simply call `controller.abort()` here results in AbortError.
    // I don't know why but this supresses the error.
    await Promise.race([stdinTimer, shutdownTimer]).finally(() =>
      controller.abort()
    );
  });
});

Deno.test("Error for erroneous command-line argument", async (t) => {
  async function run(args: string[]): Promise<Deno.CommandOutput | undefined> {
    const controller = new AbortController();
    const command = new Deno.Command(defaultExecutable, {
      signal: controller.signal,
      args,
      stdin: "null",
      stdout: "piped",
      stderr: "piped",
    });
    const timer = setTimeout(1000, undefined, { signal: controller.signal });
    const child = command.spawn();
    return await Promise.race([child.output(), timer]).finally(() =>
      controller.abort()
    );
  }

  function assertNotUndefined<T>(v: T | undefined): asserts v is T {
    if (v == undefined) {
      throw Error("Unexpectedly got undefined!!");
    }
  }

  function decode<T extends AllowSharedBufferSource>(v: T): string {
    return new TextDecoder().decode(v);
  }

  await t.step("Unknown argument", async () => {
    const out = await run(["--hogehoge"]);
    assertNotUndefined(out);
    assertEquals(out.code, 255);
    assertMatch(decode(out.stderr), /Unknown argument: --hogehoge/);
  });

  await t.step("No argument after timeout option", async (t) => {
    await t.step("--timeout", async () => {
      const out = await run(["--timeout"]);
      assertNotUndefined(out);
      assertEquals(out.code, 255);
      assertMatch(decode(out.stderr), /No argument after '--timeout'/);
    });
    await t.step("--teardown-timeout", async () => {
      const out = await run(["--teardown-timeout"]);
      assertNotUndefined(out);
      assertEquals(out.code, 255);
      assertMatch(decode(out.stderr), /No argument after '--teardown-timeout'/);
    });
  });

  await t.step("Invalid number for timeout parameter", async (t) => {
    await t.step("Non number", async () => {
      const out = await run(["--timeout", "hoge"]);
      assertNotUndefined(out);
      assertEquals(out.code, 255);
      assertMatch(decode(out.stderr), /Invalid argument after '--timeout'/);
    });

    await t.step("Partially number", async () => {
      const out = await run(["--timeout", "100x"]);
      assertNotUndefined(out);
      assertEquals(out.code, 255);
      assertMatch(decode(out.stderr), /Invalid argument after '--timeout'/);
    });
  });
});
