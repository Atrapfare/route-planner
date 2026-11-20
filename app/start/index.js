// Builds, starts, measures and cleans up the project, on Windows, macOS and Linux.
// Run it from the project root (or anywhere else); paths on the command line are relative to
// the folder you run it from. `node app/start help` lists everything.
"use strict";

const { spawn, spawnSync } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");

const APP = path.join(__dirname, "..");
const BACKEND = path.join(APP, "backend");
const FRONTEND = path.join(APP, "frontend");
const isWindows = process.platform === "win32";

const USAGE = `Usage: node app/start [command] [debug|release] --pbf NAME [options]

Commands:
  (none)            build the backend, then start backend and frontend
                    (http://localhost:5173, or the next free port)
  measure [DIR]     build, run the slider measurement series, write the CSVs
                    to DIR (default: measure/) and exit
  bench-alt         build, compare Dijkstra against ALT and exit
  build             only build the backend
  clean             delete build output, graph caches, frontend dist/ and node_modules/
  help              show this help

debug | release     CMake preset (default: release)

Options (passed to the backend):
  --pbf NAME        name of the OSM extract in app/backend/data/osm/graphs/
                    (.osm.pbf may be left out); required whenever the backend
                    runs (start, measure, bench-alt), no default
  --srtm DIR        folder with the SRTM .hgt tiles (default: app/backend/data/srtm)
  --rebuild         build the graph again even if its cache exists
  --threads N       threads for the round-trip methods (default: number of cores, at most 8)
  --no-alt          route without ALT landmarks
  --port N          port of the backend (default: 8080); the frontend is forwarded to it
  --starts N        measure only: random start points in addition to the 5
                    named places (default: 100)

Examples:
  node app/start --pbf baden-wuerttemberg-261115
  node app/start debug --rebuild --pbf germany-latest --srtm ../maps/srtm
  node app/start measure results/run1 --pbf baden-wuerttemberg-261115 --starts 50 --threads 4`;

const INSTALL_HINTS = {
    linux: {
        cmake: "sudo apt install cmake",
        ninja: "sudo apt install ninja-build",
        "g++": "sudo apt install g++-14",
        npm: "nvm install --lts (https://github.com/nvm-sh/nvm)",
    },
    darwin: { cmake: "brew install cmake", ninja: "brew install ninja", npm: "brew install node" },
    win32: {
        cmake: "cmake.org or MSYS2",
        ninja: "MSYS2: pacman -S mingw-w64-ucrt-x86_64-ninja",
        npm: "nodejs.org",
    },
};

function step(message) {
    console.log(`==> ${message}`);
}

function fail(message, showUsage = false) {
    console.error(`Error: ${message}`);
    if (showUsage) {
        console.error(`\n${USAGE}`);
    }
    process.exit(1);
}


const COMMANDS = ["serve", "measure", "bench-alt", "build", "clean", "help"];
const RUNS_BACKEND = ["serve", "measure", "bench-alt"]; // only these take backend options

function parseArgs(argv) {
    const args = { command: "serve", preset: "release", backendArgs: [], measureDir: null, port: "8080" };
    let i = 0;
    if (COMMANDS.includes(argv[0])) {
        args.command = argv[0];
        i = 1;
    }
    let presetGiven = false;
    const valueOf = (flag) => {
        const v = argv[++i];
        if (v === undefined || v.startsWith("--")) {
            fail(`${flag} needs a value`, true);
        }
        return v;
    };
    const backendOption = (flag) => {
        if (!RUNS_BACKEND.includes(args.command)) {
            fail(`${flag} does not work with '${args.command}'`, true);
        }
    };

    for (; i < argv.length; i++) {
        const a = argv[i];
        if (a === "--help" || a === "-h") {
            args.command = "help";
        } else if (a === "debug" || a === "release") {
            if (presetGiven) {
                fail("give debug or release only once", true);
            }
            presetGiven = true;
            args.preset = a;
        } else if (a === "--pbf") {
            backendOption(a);
            const v = valueOf(a);
            if (/[\\/]/.test(v)) {
                fail(`--pbf takes only the name of an extract in app/backend/data/osm/graphs/, got '${v}'`, true);
            }
            args.backendArgs.push(a, v);
        } else if (a === "--srtm") {
            backendOption(a);
            args.backendArgs.push(a, path.resolve(valueOf(a))); // the backend runs in app/backend
        } else if (a === "--starts") {
            if (args.command !== "measure") {
                fail(`${a} only works with 'measure'`, true);
            }
            const v = valueOf(a);
            if (!/^\d+$/.test(v)) {
                fail(`${a} needs a whole number, got '${v}'`, true);
            }
            args.backendArgs.push(a, v);
        } else if (a === "--threads" || a === "--port") {
            backendOption(a);
            const v = valueOf(a);
            if (!/^\d+$/.test(v) || (a === "--port" && (Number(v) < 1 || Number(v) > 65535))) {
                fail(`${a} needs a whole number${a === "--port" ? " from 1 to 65535" : ""}, got '${v}'`, true);
            }
            args.backendArgs.push(a, v);
            if (a === "--port") {
                args.port = v;
            }
        } else if (a === "--rebuild" || a === "--no-alt") {
            backendOption(a);
            args.backendArgs.push(a);
        } else if (args.command === "measure" && args.measureDir === null && !a.startsWith("-")) {
            args.measureDir = a;
        } else {
            fail(`unknown argument '${a}'`, true);
        }
    }
    if (args.command === "clean" && presetGiven) {
        fail("clean takes no further arguments", true);
    }
    if (RUNS_BACKEND.includes(args.command) && !args.backendArgs.includes("--pbf")) {
        fail("--pbf is required, e.g. --pbf baden-wuerttemberg-261115 (a file in app/backend/data/osm/graphs/)", true);
    }
    return args;
}


// Full path of an executable on PATH, or null. Searched by hand so that no shell is needed.
function findExecutable(name) {
    const exts = isWindows ? (process.env.PATHEXT || ".EXE;.CMD;.BAT").split(";") : [""];
    for (const dir of (process.env.PATH || "").split(path.delimiter)) {
        for (const ext of exts) {
            const file = path.join(dir, name + ext);
            try {
                if (fs.statSync(file).isFile()) {
                    return file;
                }
            } catch {
            }
        }
    }
    return null;
}

function requireTool(name) {
    if (!findExecutable(name)) {
        const hints = INSTALL_HINTS[process.platform] || INSTALL_HINTS.linux;
        fail(`'${name}' not found.${hints[name] ? ` Install it with: ${hints[name]}` : ""}`);
    }
}

// Runs a command in the foreground and returns its exit code. npm is a .cmd file on Windows,
// which Node only starts through a shell; everything else is started directly.
function run(cmd, args, options = {}) {
    const result = spawnSync(cmd, args, { stdio: "inherit", shell: isWindows && cmd === "npm", ...options });
    if (result.error) {
        fail(`could not start ${cmd}: ${result.error.message}`);
    }
    return result.status ?? 1;
}

function backendExe(preset) {
    return path.join(BACKEND, "build", preset, isWindows ? "route_planner.exe" : "route_planner");
}


// On Linux the default g++ can be too old for C++23 (<print> needs GCC 14). For a fresh build
// directory, take the newest g++ that is installed; CMake keeps that choice afterwards.
function compilerEnv(preset) {
    const env = { ...process.env };
    const configured = fs.existsSync(path.join(BACKEND, "build", preset, "CMakeCache.txt"));
    if (process.platform === "linux" && !env.CXX && !configured) {
        const newest = ["g++-15", "g++-14"].find(findExecutable);
        if (newest) {
            env.CXX = newest;
            step(`Using ${newest}`);
        } else if (!findExecutable("g++") && !findExecutable("clang++")) {
            requireTool("g++");
        }
    }
    return env;
}

function build(preset) {
    requireTool("cmake");
    requireTool("ninja");
    step(`Building backend (${preset})`);
    const ok =
        run("cmake", ["--preset", preset], { cwd: BACKEND, env: compilerEnv(preset) }) === 0 &&
        run("cmake", ["--build", "--preset", preset], { cwd: BACKEND }) === 0;
    if (!ok) {
        fail(
            "building the backend failed.\n" +
                "If the error is about <print> or C++23, the compiler is too old (GCC >= 14 or Clang >= 18 needed):\n" +
                `install a newer one, delete app/backend/build/${preset} and run this again.`,
        );
    }
}


function runBackend(preset, backendArgs) {
    step(`Running backend ${backendArgs.join(" ")}`);
    process.exit(run(backendExe(preset), backendArgs, { cwd: BACKEND }));
}

// Backend in the background; as soon as it has loaded the graph, the frontend dev server.
// Ctrl+C (or either of them stopping) stops both.
function serve(preset, backendArgs, port) {
    requireTool("npm");
    if (!fs.existsSync(path.join(FRONTEND, "node_modules"))) {
        step("Installing frontend dependencies (npm ci: exactly package-lock.json, never rewrites it)");
        if (run("npm", ["ci"], { cwd: FRONTEND }) !== 0) {
            fail("npm ci failed (is package-lock.json in sync with package.json?)");
        }
    }

    step("Starting backend");
    const readyFile = path.join(BACKEND, "build", preset, "backend.ready");
    fs.rmSync(readyFile, { force: true });
    const backend = spawn(backendExe(preset), ["--ready-file", readyFile, ...backendArgs], {
        cwd: BACKEND,
        stdio: "inherit",
    });
    let frontend = null;
    let stopping = false;
    let poll = null;

    const running = (child) => child && child.exitCode === null && child.signalCode === null;
    const stop = (code) => {
        if (stopping) {
            return;
        }
        stopping = true;
        clearInterval(poll);
        process.exitCode = code;
        for (const child of [frontend, backend]) {
            if (running(child)) {
                child.kill();
            }
        }
        fs.rmSync(readyFile, { force: true });
    };
    // Ctrl+C reaches all three processes through the terminal; this only cleans up what is left.
    process.on("SIGINT", () => stop(0));
    process.on("SIGTERM", () => stop(0));

    backend.on("error", (err) => {
        console.error(`Could not start the backend: ${err.message}`);
        stop(1);
    });
    backend.on("exit", (code, signal) => {
        if (stopping || signal === "SIGINT") {
            return stop(0);
        }
        console.error(frontend ? `Backend stopped (${code ?? signal})` : "Backend exited before it was ready");
        stop(code || 1);
    });

    step("Waiting for backend to be ready");
    poll = setInterval(() => {
        if (!fs.existsSync(readyFile)) {
            return;
        }
        clearInterval(poll);
        step("Backend ready");
        step("Starting frontend (stop with Ctrl+C)");
        // Vite directly instead of `npm run dev`: one process without a shell in between,
        // so stopping it works the same on every system.
        frontend = spawn(process.execPath, [path.join(FRONTEND, "node_modules", "vite", "bin", "vite.js")], {
            cwd: FRONTEND,
            stdio: "inherit",
            env: { ...process.env, BACKEND_PORT: port }, // read by vite.config.ts
        });
        frontend.on("error", (err) => {
            console.error(`Could not start the frontend: ${err.message}`);
            stop(1);
        });
        frontend.on("exit", (code, signal) => stop(stopping || signal === "SIGINT" ? 0 : (code ?? 0)));
    }, 200);
}

// Removes only generated files. Raw data (.osm.pbf, .hgt) is never touched.
function clean() {
    const data = path.join(BACKEND, "data");
    const caches = fs.existsSync(data)
        ? fs
              .readdirSync(data)
              .filter((f) => /^(.+\.)?(graph|landmarks)\.bin(\.tmp)?$/.test(f))
              .map((f) => path.join(data, f))
        : [];
    const targets = [
        path.join(BACKEND, "build"),
        ...caches,
        path.join(FRONTEND, "dist"),
        path.join(FRONTEND, "node_modules"),
    ];
    for (const target of targets) {
        const rel = path.relative(path.join(APP, ".."), target);
        if (!fs.existsSync(target)) {
            console.log(`    skip    ${rel} (not present)`);
            continue;
        }
        fs.rmSync(target, { recursive: true, force: true, maxRetries: 3 });
        console.log(`    removed ${rel}`);
    }
    step("Clean done");
}


const args = parseArgs(process.argv.slice(2));
switch (args.command) {
    case "help":
        console.log(USAGE);
        break;
    case "clean":
        clean();
        break;
    case "build":
        build(args.preset);
        step("Build done");
        break;
    case "measure": {
        const dir = path.resolve(args.measureDir ?? "measure"); // the backend creates it
        build(args.preset);
        runBackend(args.preset, ["--measure", dir, ...args.backendArgs]);
        break;
    }
    case "bench-alt":
        build(args.preset);
        runBackend(args.preset, ["--bench-alt", ...args.backendArgs]);
        break;
    default:
        build(args.preset);
        serve(args.preset, args.backendArgs, args.port);
}
