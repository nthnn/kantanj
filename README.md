# kantanj

`kantanj` is a small, single-file C utility that helps you create, build, run and manage minimal Java (Maven) projects from the command line. It was designed as a pragmatic, portable replacement for a shell helper script: it detects Java via `which java`, corrects `JAVA_HOME` when necessary, creates a basic Maven `pom.xml` and a starter `App.java`, and wraps `mvn` for building and running.

---

## Installation

```sh
curl -fsSL https://raw.githubusercontent.com/nthnn/kantanj/refs/heads/main/install.sh | bash
```

---

## Features

* Create a minimal Maven project (pom + `src/main/java/.../App.java`) from a package name.
* Build the project with Maven (executes `mvn -DskipTests package`).
* Run the project's JAR (builds automatically if needed).
* Clean the project's `target` directory.
* Best-effort install helper to install JDK 21 and Maven via `apt` (Debian/Ubuntu).
* Detects Java via `which java` and computes the correct `JAVA_HOME` (fixes cases when `JAVA_HOME` was incorrectly set to the `java` binary).
* Small, portable C code (single translation unit).

---

## Requirements

* Linux (tested on Debian/Ubuntu-like systems). Many utilities rely on standard Unix tools.
* `gcc` (to build `kantanj.c`) or any compatible C compiler.
* `mvn` (Apache Maven) — `kantanj` can attempt to install Maven via `apt` when using `install` command.
* `java` — a JDK (OpenJDK/Temurin) is required to build and run projects. `kantanj` attempts to detect and fix `JAVA_HOME` where possible.

> Note: The built-in `install` command attempts `apt` installs and Adoptium repo steps; it is not guaranteed to work for all distros. If your distro is not Debian/Ubuntu-based, install Java and Maven using your platform package manager.

---

## Quick start

1. Build the program:

```bash
gcc -O2 -std=c11 -Wall -Wextra -o kantanj kantanj.c
```

2. (Optional) Install to `/usr/local/bin`:

```bash
sudo install -m 0755 kantanj /usr/local/bin/kantanj
```

3. Create a project:

```bash
kantanj create helloworld com.example.hello
```

4. Build:

```bash
kantanj build helloworld
```

5. Run:

```bash
kantanj run helloworld arg1 arg2
```

6. Clean:

```bash
kantanj clean helloworld
```

---

## Commands & examples

```
Usage: kantanj <command> [args]
Commands:
  install                    Install JDK 21 and Maven (attempt via apt)
  create <name> <package>    Create a basic maven project (e.g. com.example)
  build <name>               Build project using mvn (uses ~/.m2/settings.xml)
  run <name> [args...]       Run project's jar (builds if needed)
  clean <name>               Remove project's target directory
```

### Examples

Create a project `helloworld` with package `com.example.hello`:

```bash
kantanj create helloworld com.example.hello
```

This creates:

```
helloworld/
  pom.xml
  src/main/java/com/example/hello/App.java
```

The `pom.xml` will use `groupId = com.example.hello` and `artifactId = helloworld`. The main class will be `com.example.hello.App`.

Build:

```bash
kantanj build helloworld
```

Run (passes args to Java program):

```bash
kantanj run helloworld foo bar
```

Clean:

```bash
kantanj clean helloworld
```

Install prerequisites (try to install JDK 21 + Maven via apt; may prompt for `sudo`):

```bash
sudo kantanj install
```

or, from non-root:

```bash
kantanj install
```

`kantanj` will try to use `sudo` when needed.

---

## How `JAVA_HOME` is handled

Many Java tools (including Maven) expect `JAVA_HOME` to refer to the **JDK home directory** (for example `/usr/lib/jvm/temurin-21-jdk-amd64`), not to the `java` binary itself.

`kantanj`:

1. Detects Java using `which java` (so it follows the user's PATH).
2. Resolves symlinks with `realpath`.
3. Converts the path of the `java` binary to a reasonable `JAVA_HOME` (if `.../bin/java` is detected, it uses the parent directory as `JAVA_HOME`).
4. Sets `JAVA_HOME` for the process that runs Maven (so `mvn` sees a valid `JAVA_HOME` even if the environment was set wrongly).

This approach fixes the common situation where `JAVA_HOME` is accidentally set to `/usr/lib/jvm/temurin-21-jdk-amd64/bin/java` (binary) instead of `/usr/lib/jvm/temurin-21-jdk-amd64` (home).

---

## Development / Build

Build locally:

```bash
gcc -O2 -std=c11 -Wall -Wextra -o kantanj kantanj.c
```

Turn on more warnings (optional):

```bash
gcc -O2 -std=c11 -Wall -Wextra -Wpedantic -o kantanj kantanj.c
```

Run the program locally without installing:

```bash
./kantanj create myproject com.example.myproj
./kantanj build myproject
././kantanj run myproject
```

If changes are made and you want to install the binary:

```bash
sudo install -m 0755 kantanj /usr/local/bin/kantanj
```

---

## Design & implementation notes

* `kantanj.c` is a single C file intentionally kept self-contained for simplicity.
* The program uses `system()` for a few operations (e.g. calling `mvn`, `apt-get`, or `rm -rf`). This keeps the code straightforward, but be mindful of shell injection if you integrate the tool into other automated flows — do not pass untrusted data into `kantanj` commands that would be composed into shell commands.
* File writing and directory creation are implemented in C (no shell required).
* `create` writes a minimal `pom.xml` with `maven-shade-plugin` configured so `package` produces an executable fat JAR.
* `install` attempts `apt` installs and a best-effort attempt to add the Adoptium apt repo. It does **not** implement tarball extraction and full `update-alternatives` registration. If `install` fails, install JDK/Maven manually for your platform.

---

## Troubleshooting

**`The JAVA_HOME environment variable is not defined correctly`** (Maven error)

* This means `JAVA_HOME` points to a path Maven does not accept (commonly the `java` binary). `kantanj` attempts to detect and correct `JAVA_HOME` automatically. To verify:

  ```bash
  which java
  realpath $(which java)
  echo $JAVA_HOME
  ```
* If `echo $JAVA_HOME` prints something ending with `/bin/java`, correct it:

  ```bash
  export JAVA_HOME=/usr/lib/jvm/temurin-21-jdk-amd64
  ```

  Then re-run `kantanj build ...`.

**Maven download failures (network / 502s)**

* `kantanj` writes a minimal `~/.m2/settings.xml` that mirrors central to `repo1.maven.org`. If your environment uses a corporate proxy or internal mirror, adjust `~/.m2/settings.xml` accordingly.

**`apt` install fails on non-Debian systems**

* The `install` command targets Debian/Ubuntu. For other distros, use the distro's package manager (dnf, yum, pacman, zypper) or install a JDK manually.

---

## License

Copyright 2025 - Nathanne Isip

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

---

## Changelog

* `v0.1` — initial single-file C implementation:

  * `create`, `build`, `run`, `clean` commands.
  * `install` helper (apt-focused).
  * `JAVA_HOME` auto-correction (computed from `which java`).
