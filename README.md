# AURORA BANK — C++17 Net Banking Project

Aurora Bank is a fictional, academic net-banking website. The **main application and banking logic run in C++17**. HTML, CSS and browser-side JavaScript are used only for the website interface.

## Project language split

The intended project architecture is approximately **75% C++ core implementation and 25% web presentation/support**:

- **C++17 (primary):** HTTP server, API routes, account creation and authentication, balance rules, deposits, withdrawals, transfers, payees, cards, deposits/investments, loans, insurance, bill payments, customer statistics, CSV statements, JSON persistence and fraud-risk scoring.
- **HTML/CSS/JavaScript (supporting):** the two website pages, visual styling, forms, charts, browser navigation and API calls.

The 75/25 figure describes the project's implementation focus. GitHub's language bar is calculated from source-file byte counts, so its exact percentages may differ from the architecture target.

## Folder structure

```text
aurora-bank-portal/
├── src/
│   ├── main.cpp             # C++ application entry point
│   ├── http_server.cpp      # Native HTTP server (Winsock / POSIX sockets)
│   ├── http_server.hpp
│   ├── bank_service.cpp     # Banking API and business rules
│   ├── bank_service.hpp
│   ├── fraud_engine.cpp     # C++ transaction risk engine
│   ├── fraud_engine.hpp
│   └── json.hpp             # Small self-contained JSON parser/writer
├── tests/
│   ├── bank_service_tests.cpp
│   └── fraud_engine_tests.cpp
├── database/
│   └── bank.json            # Fictional demonstration data
├── index.html
├── dashboard.html
├── app.js                  # Browser-side UI behaviour only
├── styles.css
├── extra.css
├── build.bat / start.bat    # Windows build and launch
├── run.sh                   # Linux/macOS build and launch
└── CMakeLists.txt
```

## Run on Windows (VS Code)

1. Install a C++17 compiler such as MinGW-w64 / MSYS2 GCC and add its `bin` directory to PATH.
2. Open this project folder in VS Code.
3. Open the terminal in the `aurora-bank-portal` folder.
4. Run `g++ --version` to confirm the compiler is available.
5. Double-click `start.bat`, or run `start.bat` in the terminal.
6. Open **http://localhost:3000** in your browser.

The build uses only the C++ standard library and the Windows socket library supplied with MinGW. **Node.js and npm are not required.**

## Run on Linux / macOS

From the project folder:

```bash
chmod +x run.sh
./run.sh
```

The script builds the C++ server and starts it on port 3000. Set the `PORT` environment variable to use another port.

## Run C++ tests

```bash
g++ -std=c++17 -O2 -pthread tests/fraud_engine_tests.cpp src/fraud_engine.cpp -o fraud_tests
./fraud_tests

g++ -std=c++17 -O2 -pthread tests/bank_service_tests.cpp src/bank_service.cpp src/fraud_engine.cpp -o bank_tests
./bank_tests
```

On Windows, add `-lws2_32` only when building the HTTP server; the two test executables do not need it.

## Demo login

- Account number: `1001`
- PIN: `1234`

## Important

This is a classroom demonstration, **not a real banking system**. Customers, balances, branches, IFSC codes and transactions are fictional. Do not use real credentials or real money. The sample PINs are stored in the demonstration JSON database for ease of testing; this is not production-grade authentication or security.
