#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <pwd.h>
#include <limits.h>

static void die(const char *fmt, ...) __attribute__((noreturn));
static void die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
}

static void infof(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "INFO: ");

    vfprintf(stdout, fmt, ap);
    va_end(ap);

    fprintf(stdout, "\n");
}

static void warnf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "WARN: ");

    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
}

static int run_cmdf(const char *fmt, ...)
{
    char cmd[8192];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);

    infof("Running: %s", cmd);

    int rc = system(cmd);
    if (rc == -1) {
        warnf("system() failed: %s", strerror(errno));
        return -1;
    }

    if (WIFEXITED(rc)) return WEXITSTATUS(rc);
    return rc;
}

static int which_exists(const char *prog)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", prog);

    return system(cmd) == 0;
}

static int detect_java_path(char *out, size_t outlen)
{
    FILE *fp = popen("which java 2>/dev/null", "r");
    if (!fp) return -1;

    if (!fgets(out, outlen, fp)) {
        pclose(fp);
        return -1;
    }
    pclose(fp);

    size_t len = strlen(out);
    if (len > 0 && out[len-1] == '\n') out[len-1] = '\0';

    return (strlen(out) > 0) ? 0 : -1;
}

static char *compute_java_home_from_binary(const char *java_path)
{
    if (!java_path) return NULL;

    char resolved[PATH_MAX];
    if (!realpath(java_path, resolved)) {
        strncpy(resolved, java_path, sizeof(resolved));
        resolved[sizeof(resolved)-1] = '\0';
    }

    char *last_slash = strrchr(resolved, '/');
    if (!last_slash) {
        return strdup("/");
    }
    *last_slash = '\0';

    size_t len = strlen(resolved);
    if (len >= 4 && strcmp(resolved + (len - 4), "/bin") == 0) {
        char *home = strdup(resolved);
        if (!home) return NULL;

        home[len - 4] = '\0';
        if (home[0] == '\0') {
            free(home);
            return strdup("/");
        }

        return home;
    }

    return strdup(resolved);
}

static int mkpath(const char *path, mode_t mode)
{
    char tmp[PATH_MAX];
    if (strlen(path) >= sizeof(tmp)) return -1;

    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp)-1] = '\0';

    size_t len = strlen(tmp);
    if (len == 0) return -1;
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;

            if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
            *p = '/';
        }
    }

    if (mkdir(tmp, mode) != 0 && errno != EEXIST) return -1;
    return 0;
}

static const char *home_dir()
{
    const char *h = getenv("HOME");
    if (h) return h;

    struct passwd *pw = getpwuid(getuid());
    if (pw) return pw->pw_dir;

    return "/";
}

static int write_file(const char *path, const char *data, mode_t mode)
{
    char tmp[PATH_MAX];
    strncpy(tmp, path, sizeof(tmp));
    tmp[sizeof(tmp)-1] = '\0';

    char *slash = strrchr(tmp, '/');
    if (slash) {
        *slash = 0;
        if (mkpath(tmp, 0755) != 0) {
            warnf("Failed to create directory %s", tmp);
            return -1;
        }
    }

    FILE *f = fopen(path, "w");
    if (!f) {
        warnf("Failed to open %s: %s", path, strerror(errno));
        return -1;
    }

    if (fwrite(data, 1, strlen(data), f) != strlen(data)) {
        warnf("Write error for %s: %s", path, strerror(errno));
        fclose(f);

        return -1;
    }

    fclose(f);
    chmod(path, mode);

    return 0;
}

static void detect_distro(char *out, size_t outlen)
{
    out[0] = '\0';

    FILE *f = fopen("/etc/os-release", "r");
    if (!f) {
        strncpy(out, "unknown", outlen);
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "ID=", 3) == 0) {
            char *v = strchr(line, '=');
            if (v) {
                v++;
                while (*v == '"' || *v == '\'') v++;

                char *end = v + strlen(v) - 1;
                while (end > v && (*end == '\n' || *end == '"' || *end == '\'')) { *end = '\0'; end--; }

                strncpy(out, v, outlen-1);
                out[outlen-1] = '\0';

                break;
            }
        }
    }

    fclose(f);
}

static int apt_install(const char *pkgs)
{
    int is_root = (getuid() == 0);
    if (!is_root) {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "sudo apt-get update && sudo apt-get install -y %s", pkgs);

        return run_cmdf("%s", cmd);
    } else {
        char cmd[1024];
        snprintf(cmd, sizeof(cmd), "apt-get update && apt-get install -y %s", pkgs);

        return run_cmdf("%s", cmd);
    }
}

static char *join_paths(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";

    size_t la = strlen(a);
    size_t lb = strlen(b);

    int need_slash = 0;
    if (la > 0 && a[la-1] != '/') need_slash = 1;

    size_t total = la + need_slash + lb + 1;
    char *out = malloc(total);

    if (!out) return NULL;
    out[0] = '\0';

    if (la) strncat(out, a, total-1);
    if (need_slash) strncat(out, "/", total-1);
    if (lb) strncat(out, b, total-1);

    return out;
}

static char *join_three(const char *a, const char *b, const char *c)
{
    char *tmp = join_paths(a, b);
    if (!tmp) return NULL;

    char *res = join_paths(tmp, c);
    free(tmp);

    return res;
}

static char *package_to_path(const char *pkg)
{
    if (!pkg) return NULL;

    size_t len = strlen(pkg);
    char *out = malloc(len + 1);

    if (!out) return NULL;
    for (size_t i = 0; i < len; ++i) out[i] = (pkg[i] == '.') ? '/' : pkg[i];

    out[len] = '\0';
    return out;
}

static int cmd_install_prereqs(void)
{
    char java_bin[PATH_MAX];
    if (detect_java_path(java_bin, sizeof(java_bin)) == 0) {
        infof("Java already detected at: %s", java_bin);
    } else {
        infof("Java not found via 'which java' — attempting to install JDK 21 via apt");

        char distro[128];
        detect_distro(distro, sizeof(distro));

        infof("Detected distro: %s", distro);
        if (apt_install("openjdk-21-jdk") == 0) {
            infof("Installed openjdk-21-jdk via apt");
        } else {
            warnf("openjdk-21-jdk apt install failed; trying openjdk-21-jdk-headless");
            if (apt_install("openjdk-21-jdk-headless") == 0) {
                infof("Installed openjdk-21-jdk-headless via apt");
            } else {
                warnf("Failed to install OpenJDK 21 via apt. Attempting Adoptium (temurin-21-jdk) repository steps.");

                (void)run_cmdf("sudo apt-get install -y wget gnupg apt-transport-https ca-certificates || true");
                (void)run_cmdf("wget -qO - https://packages.adoptium.net/artifactory/api/gpg/key/public | sudo apt-key add - || true");
                (void)run_cmdf("echo 'deb https://packages.adoptium.net/artifactory/deb/ stable main' | sudo tee /etc/apt/sources.list.d/adoptium.list > /dev/null || true");
                (void)run_cmdf("sudo apt-get update || true");

                if (apt_install("temurin-21-jdk") == 0) {
                    infof("Installed temurin-21-jdk via Adoptium");
                } else {
                    warnf("Automatic apt-based JDK installation attempts failed. Please install JDK 21 manually.");
                }
            }
        }
    }

    if (!which_exists("mvn")) {
        infof("Maven not found; attempting apt-get install maven");
        if (apt_install("maven") != 0) {
            warnf("Failed to install maven via apt. Install Maven manually from https://maven.apache.org/");
        } else {
            infof("Maven installed");
        }
    } else {
        infof("Maven already present on PATH");
    }

    const char *home = home_dir();
    char *settings_path = join_paths(home, ".m2/settings.xml");
    const char *settings_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<settings xmlns=\"http://maven.apache.org/SETTINGS/1.0.0\"\n"
        "          xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
        "          xsi:schemaLocation=\"http://maven.apache.org/SETTINGS/1.0.0\n"
        "                              https://maven.apache.org/xsd/settings-1.0.0.xsd\">\n"
        "  <mirrors>\n"
        "    <mirror>\n"
        "      <id>central-mirror-repo1</id>\n"
        "      <name>mirror of central to repo1.maven.org</name>\n"
        "      <url>https://repo1.maven.org/maven2/</url>\n"
        "      <mirrorOf>central</mirrorOf>\n"
        "    </mirror>\n"
        "  </mirrors>\n"
        "</settings>\n";
    if (settings_path) {
        if (write_file(settings_path, settings_xml, 0644) == 0) {
            infof("Wrote %s", settings_path);
        } else {
            warnf("Could not write %s", settings_path);
        }
        free(settings_path);
    } else {
        warnf("Out of memory while creating settings path");
    }

    return 0;
}

static int cmd_create_project_with_package(const char *name, const char *pkg)
{
    if (!name || name[0] == '\0') die("create requires a project name");
    if (!pkg || pkg[0] == '\0') die("create requires a project package (e.g. com.example)");

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

    char *projdir = join_paths(cwd, name);
    if (!projdir) die("Out of memory");

    if (access(projdir, F_OK) == 0) {
        free(projdir);
        die("Project directory %s already exists", projdir);
    }

    if (mkpath(projdir, 0755) != 0) {
        free(projdir);
        die("Failed to create project directory %s", projdir);
    }

    const char *pom_template =
        "<project xmlns=\"http://maven.apache.org/POM/4.0.0\"\n"
        "         xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"\n"
        "         xsi:schemaLocation=\"http://maven.apache.org/POM/4.0.0\n"
        "                             http://maven.apache.org/maven-v4_0_0.xsd\">\n"
        "  <modelVersion>4.0.0</modelVersion>\n"
        "  <groupId>%s</groupId>\n"
        "  <artifactId>%s</artifactId>\n"
        "  <version>1.0</version>\n"
        "  <properties>\n"
        "    <project.build.sourceEncoding>UTF-8</project.build.sourceEncoding>\n"
        "    <maven.compiler.source>21</maven.compiler.source>\n"
        "    <maven.compiler.target>21</maven.compiler.target>\n"
        "  </properties>\n"
        "  <dependencies>\n"
        "  </dependencies>\n"
        "  <build>\n"
        "    <plugins>\n"
        "      <plugin>\n"
        "        <groupId>org.apache.maven.plugins</groupId>\n"
        "        <artifactId>maven-shade-plugin</artifactId>\n"
        "        <version>3.3.0</version>\n"
        "        <executions>\n"
        "          <execution>\n"
        "            <phase>package</phase>\n"
        "            <goals><goal>shade</goal></goals>\n"
        "            <configuration>\n"
        "              <transformers>\n"
        "                <transformer implementation=\"org.apache.maven.plugins.shade.resource.ManifestResourceTransformer\">\n"
        "                  <mainClass>%s.App</mainClass>\n"
        "                </transformer>\n"
        "              </transformers>\n"
        "            </configuration>\n"
        "          </execution>\n"
        "        </executions>\n"
        "      </plugin>\n"
        "    </plugins>\n"
        "  </build>\n"
        "</project>\n";

    size_t needed = strlen(pom_template) + strlen(pkg) + strlen(name) + 64;
    char *pom = malloc(needed);

    if (!pom) { free(projdir); die("Out of memory"); }
    snprintf(pom, needed, pom_template, pkg, name, pkg);

    char *pom_path = join_paths(projdir, "pom.xml");
    if (!pom_path) { free(projdir); free(pom); die("Out of memory"); }

    if (write_file(pom_path, pom, 0644) != 0) {
        free(projdir); free(pom); free(pom_path);
        die("Failed to write pom.xml");
    }

    char *pkg_path = package_to_path(pkg);
    if (!pkg_path) { free(projdir); free(pom); free(pom_path); die("Out of memory"); }

    char *appdir = join_three(projdir, "src/main/java", pkg_path);
    if (!appdir) { free(projdir); free(pom); free(pom_path); free(pkg_path); die("Out of memory"); }

    if (mkpath(appdir, 0755) != 0) {
        free(projdir); free(pom); free(pom_path); free(pkg_path); free(appdir);
        die("Failed to create src dirs");
    }

    const char *appjava_template =
        "package %s;\n\n"
        "public class App {\n"
        "  public static void main(String[] args) {\n"
        "    System.out.println(\"Hello from %s!\");\n"
        "    if (args != null && args.length > 0) {\n"
        "      System.out.println(\"Args:\");\n"
        "      for (String a : args) System.out.println(\" - \" + a);\n"
        "    }\n"
        "  }\n"
        "}\n";

    size_t need_app = strlen(appjava_template) + strlen(pkg) + strlen(name) + 8;
    char *appjava = malloc(need_app);
    if (!appjava) { free(projdir); free(pom); free(pom_path); free(pkg_path); free(appdir); die("Out of memory"); }
    snprintf(appjava, need_app, appjava_template, pkg, name);

    char *appjava_path = join_paths(appdir, "App.java");
    if (!appjava_path) { free(projdir); free(pom); free(pom_path); free(pkg_path); free(appdir); free(appjava); die("Out of memory"); }
    if (write_file(appjava_path, appjava, 0644) != 0) {
        free(projdir); free(pom); free(pom_path); free(pkg_path); free(appdir); free(appjava); free(appjava_path);
        die("Failed to write App.java");
    }

    infof("Project '%s' created at %s with package '%s'", name, projdir, pkg);
    infof("Run: mvn -f '%s' package", pom_path);

    free(projdir);
    free(pom);
    free(pom_path);
    free(pkg_path);
    free(appdir);
    free(appjava);
    free(appjava_path);

    return 0;
}

static int cmd_build_project(const char *name)
{
    if (!name || name[0] == '\0') die("build requires a project name");

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

    char *projdir = join_paths(cwd, name);
    if (!projdir) die("Out of memory");

    if (access(projdir, F_OK) != 0) { free(projdir); die("Project directory %s does not exist", projdir); }

    const char *home = home_dir();
    char *settings = join_paths(home, ".m2/settings.xml");

    if (!settings) { free(projdir); die("Out of memory"); }
    if (access(settings, R_OK) != 0) {
        warnf("%s not found; proceeding without explicit settings", settings);
    }

    char java_bin[PATH_MAX];
    if (detect_java_path(java_bin, sizeof(java_bin)) == 0) {
        char *computed_home = compute_java_home_from_binary(java_bin);
        if (computed_home) {
            const char *current_jh = getenv("JAVA_HOME");
            if (current_jh == NULL) {
                setenv("JAVA_HOME", computed_home, 1);
                infof("Set JAVA_HOME=%s (computed from which java)", computed_home);
            } else {
                if (strstr(current_jh, "/bin/java") != NULL || strcmp(current_jh, java_bin) == 0) {
                    setenv("JAVA_HOME", computed_home, 1);
                    infof("Fixed JAVA_HOME to %s (was %s)", computed_home, current_jh);
                } else {
                    infof("Using existing JAVA_HOME=%s", current_jh);
                }
            }

            free(computed_home);
        } else {
            warnf("Could not compute JAVA_HOME from java binary (%s). mvn may fail.", java_bin);
        }
    } else {
        warnf("No java binary found via 'which java' — mvn may fail if JAVA_HOME is not set correctly");
    }

    int rc = run_cmdf("mvn -f '%s/pom.xml' -s '%s' -DskipTests package", projdir, settings);
    if (rc != 0) warnf("mvn build failed (code %d)", rc);
    else infof("Build succeeded");

    free(projdir);
    free(settings);

    return rc;
}

static int cmd_run_project(const char *name, int argc, char *argv[])
{
    if (!name || name[0] == '\0') die("run requires a project name");
    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

    size_t jarprefix_len = strlen(cwd) + 1 + strlen(name) + strlen("/target/") + strlen(name) + strlen("-1.0.jar") + 1;
    char *jarpath = malloc(jarprefix_len);
    if (!jarpath) die("Out of memory");

    jarpath[0] = '\0';
    strncat(jarpath, cwd, jarprefix_len - 1);
    strncat(jarpath, "/", jarprefix_len - 1);
    strncat(jarpath, name, jarprefix_len - 1);
    strncat(jarpath, "/target/", jarprefix_len - 1);
    strncat(jarpath, name, jarprefix_len - 1);
    strncat(jarpath, "-1.0.jar", jarprefix_len - 1);

    if (access(jarpath, R_OK) != 0) {
        infof("Jar %s not found; attempting to build first", jarpath);
        free(jarpath);

        if (cmd_build_project(name) != 0) die("Build failed; cannot run");

        jarprefix_len = strlen(cwd) + 1 + strlen(name) + strlen("/target/") + strlen(name) + strlen("-1.0.jar") + 1;
        jarpath = malloc(jarprefix_len);

        if (!jarpath) die("Out of memory");

        jarpath[0] = '\0';

        strncat(jarpath, cwd, jarprefix_len - 1);
        strncat(jarpath, "/", jarprefix_len - 1);
        strncat(jarpath, name, jarprefix_len - 1);
        strncat(jarpath, "/target/", jarprefix_len - 1);
        strncat(jarpath, name, jarprefix_len - 1);
        strncat(jarpath, "-1.0.jar", jarprefix_len - 1);
    }

    char java_bin[PATH_MAX];
    if (detect_java_path(java_bin, sizeof(java_bin)) != 0) {
        free(jarpath);
        die("Java runtime not found via 'which java' — please install JDK or run: sudo ./kantanj install");
    }

    size_t max_args = 4 + (size_t)argc;
    char **exec_args = calloc(max_args, sizeof(char*));
    if (!exec_args) { free(jarpath); die("Out of memory"); }

    exec_args[0] = strdup(java_bin);
    exec_args[1] = strdup("-jar");
    exec_args[2] = strdup(jarpath);

    for (int i = 0; i < argc; ++i) exec_args[3 + i] = argv[i];

    exec_args[3 + argc] = NULL;
    pid_t pid = fork();

    if (pid < 0) {
        free(jarpath);
        free(exec_args[0]); free(exec_args[1]); free(exec_args[2]); free(exec_args);
        die("fork failed: %s", strerror(errno));
    }

    if (pid == 0) {
        execvp(exec_args[0], exec_args);
        fprintf(stderr, "execvp failed: %s\n", strerror(errno));
        _exit(127);
    }

    int status;
    waitpid(pid, &status, 0);

    free(jarpath);
    free(exec_args[0]); free(exec_args[1]); free(exec_args[2]);
    free(exec_args);

    if (WIFEXITED(status)) return WEXITSTATUS(status);
    return status;
}

static int cmd_clean_project(const char *name)
{
    if (!name || name[0] == '\0') die("clean requires a project name");

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, ".");

    char *projdir = join_paths(cwd, name);
    if (!projdir) die("Out of memory");

    if (access(projdir, F_OK) != 0) { free(projdir); die("Project directory %s does not exist", projdir); }

    char *targetdir = join_paths(projdir, "target");
    if (!targetdir) { free(projdir); die("Out of memory"); }

    if (access(targetdir, F_OK) == 0) {
        infof("Removing %s", targetdir);
        if (run_cmdf("rm -rf '%s'", targetdir) != 0) warnf("Failed to remove %s", targetdir);
    } else {
        infof("No target directory to remove");
    }

    free(projdir);
    free(targetdir);

    return 0;
}

static void print_usage_and_exit(void)
{
    printf("Usage: kantanj <command> [args]\n\n");
    printf("Commands:\n");
    printf("  install                    Install JDK 21 and Maven (attempt via apt)\n");
    printf("  create <name> <package>    Create a basic maven project (e.g. com.example)\n");
    printf("  build <name>               Build project using mvn (uses ~/.m2/settings.xml)\n");
    printf("  run <name> [args...]       Run project's jar (builds if needed)\n");
    printf("  clean <name>               Remove project's target directory\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 2) print_usage_and_exit();
    const char *cmd = argv[1];

    if (strcmp(cmd, "install") == 0) {
        return cmd_install_prereqs();
    } else if (strcmp(cmd, "create") == 0) {
        if (argc < 4) die("create <project-name> <project-package>");
        return cmd_create_project_with_package(argv[2], argv[3]);
    } else if (strcmp(cmd, "build") == 0) {
        if (argc < 3) die("build requires project name");
        return cmd_build_project(argv[2]);
    } else if (strcmp(cmd, "run") == 0) {
        if (argc < 3) die("run requires project name");
        return cmd_run_project(argv[2], argc - 3, &argv[3]);
    } else if (strcmp(cmd, "clean") == 0) {
        if (argc < 3) die("clean requires project name");
        return cmd_clean_project(argv[2]);
    } else {
        print_usage_and_exit();
    }
    return 0;
}
