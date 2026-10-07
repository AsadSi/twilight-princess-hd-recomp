// Windows: the pinned embeddable Python for tools/installer/setup.py (no PowerShell, no installation, no
// admin rights). Downloaded with WinHTTP, checked against the SHA-256 in tools/installer/toolchains.json and
// unpacked into <data>\python\<dir>, where <data> is %WWHD_DATA_DIR%, the release folder's data\ (portable
// release: portable.txt) or %LOCALAPPDATA%\WWHD.
#pragma once
#ifdef _WIN32

#include <functional>
#include <string>
#include <vector>

struct PythonPin {
    std::string url, sha256, dir;  // toolchains.json: python.windows
};

// The folder Python goes into (see above). pkg: the release folder, ending with a separator.
std::string python_data_dir(const std::string& pkg);

// python.exe of the pinned Python, downloaded first when it is missing. say() gets progress lines (called on
// the calling thread). Returns false with err set (the reason, without "Setup could not get Python: ").
bool fetch_python(const std::string& pkg, const PythonPin& pin, std::string& python_exe, std::string& err,
                  const std::function<void(const std::string&)>& say);

// "Wind Waker HD.exe --console-setup ARGS" (tools\Setup in a console window.bat): fetches Python as above and
// runs setup.py ARGS in the console the program was started from, then returns setup.py's exit code
// (1 when Python could not be fetched).
int console_setup(const std::string& pkg, const PythonPin* pin, const std::string& pin_error,
                  const std::vector<std::string>& args);

#endif
