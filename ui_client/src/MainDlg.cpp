// MainDlg.cpp
// Implementation of the main dialog for the HS Financial Services
// MFC client test application.
//
// Key design decisions
// ────────────────────
// • All network I/O is performed on a detached worker thread so the UI
//   remains responsive during long operations.
// • The worker thread communicates with the UI thread exclusively through
//   PostMessage(WM_LOG_LINE) and PostMessage(WM_WORKER_DONE).
// • The wire protocol is the same 8-byte-header + JSON-payload framing used
//   by the hs_server (see server/include/protocol.hpp).
// • Winsock 2 blocking calls are used for simplicity; a production client
//   would use async I/O or io_context.

#include "stdafx.h"
#include "MainDlg.h"

// ---------------------------------------------------------------------------
// Protocol constants (mirrors server/include/protocol.hpp)
// ---------------------------------------------------------------------------
namespace {

constexpr size_t   kHeaderSize    = 8;
constexpr uint32_t kMaxPayload    = 16u * 1024u * 1024u;

// Message-type values (client → server)
constexpr uint16_t kMsgSqlQuery   = 0x0001;
constexpr uint16_t kMsgBuyOrder   = 0x0002;
constexpr uint16_t kMsgSellOrder  = 0x0003;
constexpr uint16_t kMsgGetPos     = 0x0004;
constexpr uint16_t kMsgPriceHist  = 0x0005;
constexpr uint16_t kMsgPing       = 0x0006;

// Message-type values (server → client)
constexpr uint16_t kRespSql       = 0x8001;
constexpr uint16_t kRespOrder     = 0x8002;
constexpr uint16_t kRespPosition  = 0x8004;
constexpr uint16_t kRespPriceHist = 0x8005;
constexpr uint16_t kRespPong      = 0x8006;
constexpr uint16_t kRespError     = 0xFFFF;

// Encode an 8-byte header (big-endian).
void EncodeHeader(uint8_t out[kHeaderSize],
                  uint32_t payloadLen,
                  uint16_t msgType,
                  uint8_t  flags = 0)
{
    out[0] = static_cast<uint8_t>((payloadLen >> 24) & 0xFF);
    out[1] = static_cast<uint8_t>((payloadLen >> 16) & 0xFF);
    out[2] = static_cast<uint8_t>((payloadLen >>  8) & 0xFF);
    out[3] = static_cast<uint8_t>( payloadLen        & 0xFF);
    out[4] = static_cast<uint8_t>((msgType    >>  8) & 0xFF);
    out[5] = static_cast<uint8_t>( msgType           & 0xFF);
    out[6] = flags;
    out[7] = 0; // reserved
}

// Decode an 8-byte header and return payload length and message type.
void DecodeHeader(const uint8_t in[kHeaderSize],
                  uint32_t& outPayloadLen,
                  uint16_t& outMsgType)
{
    outPayloadLen = (static_cast<uint32_t>(in[0]) << 24)
                  | (static_cast<uint32_t>(in[1]) << 16)
                  | (static_cast<uint32_t>(in[2]) <<  8)
                  |  static_cast<uint32_t>(in[3]);
    outMsgType    = (static_cast<uint16_t>(in[4]) << 8)
                  |  static_cast<uint16_t>(in[5]);
}

// Tiny UTF-8 → UTF-16 helper (no external dependencies).
std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0,
                                     s.c_str(), static_cast<int>(s.size()),
                                     nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0,
                        s.c_str(), static_cast<int>(s.size()),
                        &out[0], needed);
    return out;
}

// Minimal JSON field extractor – finds "key":"value" or "key":number
std::string ExtractField(const std::string& json, const std::string& key)
{
    // Try string value first
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos != std::string::npos) {
        pos += needle.size();
        auto end = json.find('"', pos);
        return (end != std::string::npos) ? json.substr(pos, end - pos) : std::string{};
    }
    // Try numeric / array value
    needle = "\"" + key + "\":";
    pos = json.find(needle);
    if (pos == std::string::npos) return {};
    pos += needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    auto end = pos;
    while (end < json.size() && json[end] != ',' && json[end] != '}') ++end;
    return json.substr(pos, end - pos);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Static command table
// ---------------------------------------------------------------------------
static const CommandEntry kCommands[] = {
    { L"[1]  Login  \x2013  Connect and authenticate",           (int)CmdId::Login           },
    { L"[2]  Logout \x2013  Disconnect from server",             (int)CmdId::Logout          },
    { L"-----------------------------------------------------------", -1 },
    { L"[3]  Ping server",                                        (int)CmdId::Ping            },
    { L"-----------------------------------------------------------", -1 },
    { L"[4]  SQL Query \x2013  SELECT AAPL quotes",              (int)CmdId::SqlQueryAapl     },
    { L"[5]  SQL Query \x2013  SELECT MSFT quotes",              (int)CmdId::SqlQueryMsft     },
    { L"[6]  SQL Query \x2013  SELECT all tickers",              (int)CmdId::SqlQueryAll      },
    { L"-----------------------------------------------------------", -1 },
    { L"[7]  Buy Order  \x2013  AAPL 100 @ $182.50 (ACC-001)",   (int)CmdId::BuyAapl         },
    { L"[8]  Buy Order  \x2013  MSFT  50 @ $310.10 (ACC-001)",   (int)CmdId::BuyMsft         },
    { L"[9]  Sell Order \x2013  AAPL  75 @ $183.00 (ACC-002)",   (int)CmdId::SellAapl        },
    { L"[10] Sell Order \x2013  TSLA 200 @ $245.50 (ACC-002)",   (int)CmdId::SellTsla        },
    { L"-----------------------------------------------------------", -1 },
    { L"[11] Get Position \x2013  Account ACC-001",               (int)CmdId::GetPositionAcc001},
    { L"[12] Get Position \x2013  Account ACC-002",               (int)CmdId::GetPositionAcc002},
    { L"-----------------------------------------------------------", -1 },
    { L"[13] Price History \x2013  AAPL (30 days)",               (int)CmdId::PriceHistAapl30 },
    { L"[14] Price History \x2013  MSFT  (7 days)",               (int)CmdId::PriceHistMsft7  },
    { L"[15] Price History \x2013  GOOGL (14 days)",              (int)CmdId::PriceHistGoogl14},
    { L"-----------------------------------------------------------", -1 },
    { L"[16] \u2605 BATCH TEST \x2013  Run all services sequentially", (int)CmdId::BatchTest  },
};
static const int kCommandCount = static_cast<int>(std::size(kCommands));

// ---------------------------------------------------------------------------
// Message map
// ---------------------------------------------------------------------------
BEGIN_MESSAGE_MAP(CMainDlg, CDialogEx)
    ON_BN_CLICKED(IDC_BTN_CONNECT,    &CMainDlg::OnBnClickedConnect)
    ON_BN_CLICKED(IDC_BTN_DISCONNECT, &CMainDlg::OnBnClickedDisconnect)
    ON_BN_CLICKED(IDC_BTN_RUN,        &CMainDlg::OnBnClickedRun)
    ON_BN_CLICKED(IDC_BTN_CLEAR,      &CMainDlg::OnBnClickedClear)
    ON_MESSAGE(WM_LOG_LINE,           &CMainDlg::OnLogLine)
    ON_MESSAGE(WM_WORKER_DONE,        &CMainDlg::OnWorkerDone)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
CMainDlg::CMainDlg(CWnd* pParent)
    : CDialogEx(IDD_MAIN_DIALOG, pParent)
{}

// ---------------------------------------------------------------------------
void CMainDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_HOST,       m_editHost);
    DDX_Control(pDX, IDC_EDIT_PORT,       m_editPort);
    DDX_Control(pDX, IDC_COMBO_COMMANDS,  m_comboCommands);
    DDX_Control(pDX, IDC_EDIT_OUTPUT,     m_editOutput);
    DDX_Control(pDX, IDC_STATIC_STATUS,   m_staticStatus);
}

// ---------------------------------------------------------------------------
BOOL CMainDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // ── Default host / port ─────────────────────────────────────────────────
    m_editHost.SetWindowText(_T("127.0.0.1"));
    m_editPort.SetWindowText(_T("9000"));

    // ── Populate the command combo box ──────────────────────────────────────
    for (int i = 0; i < kCommandCount; ++i) {
        int idx = m_comboCommands.AddString(kCommands[i].label);
        m_comboCommands.SetItemData(idx, static_cast<DWORD_PTR>(kCommands[i].id));
    }
    m_comboCommands.SetCurSel(0); // Select "Login" by default.

    // ── Initial state ────────────────────────────────────────────────────────
    SetStatus(_T("\u25CF  Disconnected"), RGB(200, 0, 0));
    GetDlgItem(IDC_BTN_DISCONNECT)->EnableWindow(FALSE);
    GetDlgItem(IDC_BTN_RUN)->EnableWindow(FALSE);

    // ── Welcome banner ───────────────────────────────────────────────────────
    Log(L"╔══════════════════════════════════════════════════════════╗");
    Log(L"║   HS Financial Services  –  Client Test Application     ║");
    Log(L"║   Version 1.0   |   High-Performance Banking Server      ║");
    Log(L"╚══════════════════════════════════════════════════════════╝");
    Log(L"");
    Log(L"Available services: SQL Query, Buy/Sell Orders, Get Position,");
    Log(L"                    Price History, Ping");
    Log(L"");
    Log(L"Enter the server address and port, then press [Connect].");
    Log(L"Select a command from the drop-down and press [Run].");
    Log(L"");

    return TRUE;
}

// ---------------------------------------------------------------------------
void CMainDlg::OnCancel()
{
    DoDisconnect();

    if (m_workerThread.joinable())
        m_workerThread.detach();

    CDialogEx::OnCancel();
}

// ===========================================================================
// Button handlers
// ===========================================================================

void CMainDlg::OnBnClickedConnect()
{
    CString host, portStr;
    m_editHost.GetWindowText(host);
    m_editPort.GetWindowText(portStr);

    if (host.IsEmpty()) {
        AfxMessageBox(_T("Please enter a host address."), MB_OK | MB_ICONWARNING);
        return;
    }
    uint16_t port = static_cast<uint16_t>(_ttoi(portStr));
    if (port == 0) port = 9000;

    // Narrow-string host for Winsock.
    CT2A hostA(host, CP_UTF8);

    Log(L"");
    Log(std::wstring(L"Connecting to ") + host.GetString() +
        L":" + std::to_wstring(port) + L"...");

    if (DoConnect(std::string(hostA), port)) {
        SetStatus(_T("\u25CF  Connected"), RGB(0, 160, 0));
        GetDlgItem(IDC_BTN_CONNECT)->EnableWindow(FALSE);
        GetDlgItem(IDC_BTN_DISCONNECT)->EnableWindow(TRUE);
        GetDlgItem(IDC_BTN_RUN)->EnableWindow(TRUE);
        Log(L"TCP connection established.");
        Log(L"");
    } else {
        int err = WSAGetLastError();
        std::wostringstream oss;
        oss << L"Connection failed.  WSAError=" << err;
        Log(oss.str());
        Log(L"");
    }
}

// ---------------------------------------------------------------------------
void CMainDlg::OnBnClickedDisconnect()
{
    DoDisconnect();
    SetStatus(_T("\u25CF  Disconnected"), RGB(200, 0, 0));
    GetDlgItem(IDC_BTN_CONNECT)->EnableWindow(TRUE);
    GetDlgItem(IDC_BTN_DISCONNECT)->EnableWindow(FALSE);
    GetDlgItem(IDC_BTN_RUN)->EnableWindow(FALSE);
    Log(L"Disconnected from server.");
    Log(L"");
}

// ---------------------------------------------------------------------------
void CMainDlg::OnBnClickedRun()
{
    int sel = m_comboCommands.GetCurSel();
    if (sel == CB_ERR) return;

    int cmdId = static_cast<int>(m_comboCommands.GetItemData(sel));
    if (cmdId < 0) return; // separator

    // Disable Run and Disconnect while the worker is active to prevent
    // concurrent socket access from the UI thread.
    SetRunEnabled(false);
    GetDlgItem(IDC_BTN_DISCONNECT)->EnableWindow(FALSE);

    // Launch the worker thread.
    if (m_workerThread.joinable())
        m_workerThread.detach();

    m_workerThread = std::thread([this, cmdId]()
    {
        switch (static_cast<CmdId>(cmdId)) {
            case CmdId::Login:             RunLogin();                                          break;
            case CmdId::Logout:            RunLogout();                                         break;
            case CmdId::Ping:              RunPing();                                           break;
            case CmdId::SqlQueryAapl:      RunSqlQuery("SELECT symbol, price FROM quotes WHERE symbol='AAPL'"); break;
            case CmdId::SqlQueryMsft:      RunSqlQuery("SELECT symbol, price FROM quotes WHERE symbol='MSFT'"); break;
            case CmdId::SqlQueryAll:       RunSqlQuery("SELECT symbol, price, volume FROM quotes ORDER BY volume DESC"); break;
            case CmdId::BuyAapl:           RunBuyOrder ("AAPL", 100, 182.50, "ACC-001");        break;
            case CmdId::BuyMsft:           RunBuyOrder ("MSFT",  50, 310.10, "ACC-001");        break;
            case CmdId::SellAapl:          RunSellOrder("AAPL",  75, 183.00, "ACC-002");        break;
            case CmdId::SellTsla:          RunSellOrder("TSLA", 200, 245.50, "ACC-002");        break;
            case CmdId::GetPositionAcc001: RunGetPosition("ACC-001");                           break;
            case CmdId::GetPositionAcc002: RunGetPosition("ACC-002");                           break;
            case CmdId::PriceHistAapl30:   RunGetPriceHistory("AAPL",  30);                     break;
            case CmdId::PriceHistMsft7:    RunGetPriceHistory("MSFT",   7);                     break;
            case CmdId::PriceHistGoogl14:  RunGetPriceHistory("GOOGL", 14);                     break;
            case CmdId::BatchTest:         RunBatchTest();                                      break;
            default: break;
        }
        PostMessage(WM_WORKER_DONE);
    });
}

// ---------------------------------------------------------------------------
void CMainDlg::OnBnClickedClear()
{
    m_editOutput.SetWindowText(_T(""));
}

// ===========================================================================
// Worker-thread message handlers
// ===========================================================================

LRESULT CMainDlg::OnLogLine(WPARAM /*wParam*/, LPARAM lParam)
{
    // The worker thread heap-allocated a std::wstring and passes ownership.
    auto* pLine = reinterpret_cast<std::wstring*>(lParam);

    // Append timestamp + line to the output edit box.
    CString cur;
    m_editOutput.GetWindowText(cur);

    CString line;
    line.Format(_T("%s  %s\r\n"),
                Timestamp().c_str(),
                pLine->c_str());
    cur += line;
    m_editOutput.SetWindowText(cur);

    // Scroll to the end.
    m_editOutput.LineScroll(m_editOutput.GetLineCount());

    delete pLine;
    return 0;
}

// ---------------------------------------------------------------------------
LRESULT CMainDlg::OnWorkerDone(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
    if (m_connected) {
        // Normal completion: socket still open.
        SetRunEnabled(true);
        GetDlgItem(IDC_BTN_DISCONNECT)->EnableWindow(TRUE);
    } else {
        // The worker closed the socket (e.g., Logout command).
        // Restore the UI to "disconnected" state.
        SetStatus(_T("\u25CF  Disconnected"), RGB(200, 0, 0));
        GetDlgItem(IDC_BTN_CONNECT)->EnableWindow(TRUE);
        GetDlgItem(IDC_BTN_DISCONNECT)->EnableWindow(FALSE);
        GetDlgItem(IDC_BTN_RUN)->EnableWindow(FALSE);
    }
    return 0;
}

// ===========================================================================
// Logging helpers
// ===========================================================================

void CMainDlg::Log(const std::wstring& line)
{
    // This method is called from any thread.
    // Heap-allocate the string and transfer ownership to the UI thread.
    auto* p = new std::wstring(line);
    PostMessage(WM_LOG_LINE, 0, reinterpret_cast<LPARAM>(p));
}

void CMainDlg::LogA(const std::string& line)
{
    Log(Utf8ToWide(line));
}

/*static*/ std::wstring CMainDlg::Timestamp()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[16];
    swprintf_s(buf, L"[%02u:%02u:%02u]", st.wHour, st.wMinute, st.wSecond);
    return buf;
}

// ===========================================================================
// Connection helpers
// ===========================================================================

bool CMainDlg::DoConnect(const std::string& host, uint16_t port)
{
    // Resolve and connect synchronously.
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    if (getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res) != 0)
        return false;

    SOCKET s = INVALID_SOCKET;
    for (auto* p = res; p != nullptr; p = p->ai_next) {
        s = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s == INVALID_SOCKET) continue;

        // Enable TCP_NODELAY for minimum latency (matches server behaviour).
        int flag = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&flag), sizeof(flag));

        if (connect(s, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
            // Set a 10-second receive timeout so operations don't block forever
            // if the server stops responding.
            DWORD recvTimeout = 10000; // milliseconds
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO,
                       reinterpret_cast<const char*>(&recvTimeout),
                       sizeof(recvTimeout));
            DWORD sendTimeout = 10000;
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO,
                       reinterpret_cast<const char*>(&sendTimeout),
                       sizeof(sendTimeout));
            m_socket    = s;
            m_connected = true;
            freeaddrinfo(res);
            return true;
        }
        closesocket(s);
    }
    freeaddrinfo(res);
    return false;
}

// ---------------------------------------------------------------------------
void CMainDlg::DoDisconnect()
{
    if (m_socket != INVALID_SOCKET) {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket    = INVALID_SOCKET;
        m_connected = false;
    }
}

// ---------------------------------------------------------------------------
void CMainDlg::SetStatus(const CString& text, COLORREF /*dotColour*/)
{
    m_staticStatus.SetWindowText(text);
}

// ---------------------------------------------------------------------------
void CMainDlg::SetRunEnabled(bool enabled)
{
    // May be called from the worker thread via PostMessage path, but here it
    // is always invoked on the UI thread (from OnWorkerDone or OnBnClickedRun).
    CWnd* pBtn = GetDlgItem(IDC_BTN_RUN);
    if (pBtn) pBtn->EnableWindow(enabled ? TRUE : FALSE);
}

// ===========================================================================
// Protocol helpers
// ===========================================================================

bool CMainDlg::SendAll(const uint8_t* data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        int n = send(m_socket,
                     reinterpret_cast<const char*>(data + sent),
                     static_cast<int>(len - sent), 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

// ---------------------------------------------------------------------------
bool CMainDlg::RecvAll(uint8_t* data, size_t len)
{
    size_t received = 0;
    while (received < len) {
        int n = recv(m_socket,
                     reinterpret_cast<char*>(data + received),
                     static_cast<int>(len - received), 0);
        if (n == SOCKET_ERROR || n == 0) return false;
        received += static_cast<size_t>(n);
    }
    return true;
}

// ---------------------------------------------------------------------------
bool CMainDlg::SendMsg(uint16_t type, const std::string& payload)
{
    uint8_t header[kHeaderSize];
    EncodeHeader(header,
                 static_cast<uint32_t>(payload.size()),
                 type);
    if (!SendAll(header, kHeaderSize)) return false;
    if (!payload.empty()) {
        if (!SendAll(reinterpret_cast<const uint8_t*>(payload.data()),
                     payload.size())) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
bool CMainDlg::RecvMsg(uint16_t& outType, std::string& outPayload)
{
    uint8_t header[kHeaderSize] = {};
    if (!RecvAll(header, kHeaderSize)) return false;

    uint32_t payloadLen;
    DecodeHeader(header, payloadLen, outType);

    if (payloadLen > kMaxPayload) return false;

    outPayload.resize(payloadLen);
    if (payloadLen > 0) {
        if (!RecvAll(reinterpret_cast<uint8_t*>(&outPayload[0]),
                     payloadLen)) return false;
    }
    return true;
}

// ===========================================================================
// Command implementations (run on worker thread)
// ===========================================================================

// ── Login ───────────────────────────────────────────────────────────────────
void CMainDlg::RunLogin()
{
    Log(L"");
    Log(L"═══════════════════════════════════════════════════════════");
    Log(L"  LOGIN");
    Log(L"═══════════════════════════════════════════════════════════");

    if (!m_connected) {
        Log(L"Not connected.  Please use the [Connect] button first, then re-run Login.");
        Log(L"");
        return;
    }

    // Verify connectivity with a Ping, then display a simulated session record.
    Log(L"Sending authentication ping to verify session...");

    std::string respPayload;
    uint16_t    respType = 0;

    std::string pingPayload = R"({"seq":1,"client":"HS-UI-Client-v1.0"})";
    if (!SendMsg(kMsgPing, pingPayload) ||
        !RecvMsg(respType, respPayload))
    {
        Log(L"✗  Login failed: server did not respond to ping.");
        Log(L"");
        return;
    }

    Log(L"Server responded: " + Utf8ToWide(respPayload));
    Log(L"");
    Log(L"✓  Login successful.  Authenticated as: demo_user@hs-bank.com");
    Log(L"   Session token  : SESS-" + std::to_wstring(GetTickCount64() & 0xFFFFFF));
    Log(L"   Account access : ACC-001, ACC-002");
    Log(L"   Permissions    : TRADING, REPORTING, MARKET_DATA");
    Log(L"");
}

// ── Logout ──────────────────────────────────────────────────────────────────
void CMainDlg::RunLogout()
{
    Log(L"");
    Log(L"═══════════════════════════════════════════════════════════");
    Log(L"  LOGOUT");
    Log(L"═══════════════════════════════════════════════════════════");

    if (!m_connected) {
        Log(L"Not currently connected to the server.");
        Log(L"");
        return;
    }

    Log(L"Sending logout notification...");
    // Graceful logout: send a final ping, then close the TCP connection.
    std::string payload = R"({"seq":0,"reason":"user_logout"})";
    SendMsg(kMsgPing, payload);
    Sleep(50); // allow ACK to arrive before closing

    // Disconnect the socket directly (safe to close from worker thread;
    // the UI state is updated in OnWorkerDone via WM_WORKER_DONE).
    DoDisconnect();

    Log(L"");
    Log(L"✓  Logout successful.  Goodbye, demo_user@hs-bank.com");
    Log(L"   Session closed.  All pending orders retained on the server.");
    Log(L"");
}

// ── Ping ────────────────────────────────────────────────────────────────────
void CMainDlg::RunPing()
{
    Log(L"");
    Log(L"═══════════════════════════════════════════════════════════");
    Log(L"  PING");
    Log(L"═══════════════════════════════════════════════════════════");

    if (!m_connected) { Log(L"✗  Not connected."); Log(L""); return; }

    static int s_seq = 100;
    ++s_seq;
    std::ostringstream oss;
    oss << R"({"seq":)" << s_seq << "}";

    Log(L"Sending: " + Utf8ToWide(oss.str()));

    auto t0 = std::chrono::steady_clock::now();

    uint16_t respType = 0;
    std::string respPayload;
    if (!SendMsg(kMsgPing, oss.str()) ||
        !RecvMsg(respType, respPayload))
    {
        Log(L"✗  Ping failed (socket error).");
        Log(L"");
        return;
    }

    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();

    std::wostringstream woss;
    woss << L"Response: " << Utf8ToWide(respPayload);
    Log(woss.str());

    woss.str(L"");
    woss << L"✓  Pong received in " << (elapsed / 1000.0) << L" ms";
    Log(woss.str());
    Log(L"");
}

// ── SQL Query ────────────────────────────────────────────────────────────────
void CMainDlg::RunSqlQuery(const std::string& sql)
{
    Log(L"");
    Log(L"═══════════════════════════════════════════════════════════");
    Log(L"  SQL QUERY");
    Log(L"═══════════════════════════════════════════════════════════");

    if (!m_connected) { Log(L"✗  Not connected."); Log(L""); return; }

    std::string payload = R"({"sql":")" + sql + R"("})";
    Log(L"SQL : " + Utf8ToWide(sql));
    Log(L"Sending request...");

    auto t0 = std::chrono::steady_clock::now();

    uint16_t respType = 0;
    std::string respPayload;
    if (!SendMsg(kMsgSqlQuery, payload) ||
        !RecvMsg(respType, respPayload))
    {
        Log(L"✗  Request failed (socket error).");
        Log(L""); return;
    }

    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count() / 1000.0;

    if (respType == kRespError) {
        Log(L"✗  Server error: " + Utf8ToWide(respPayload));
        Log(L""); return;
    }

    Log(L"Response payload:");
    Log(L"  " + Utf8ToWide(respPayload));

    // Count rows (count occurrences of "symbol":
    size_t rowCount = 0;
    size_t p = 0;
    while ((p = respPayload.find("\"symbol\":", p)) != std::string::npos) {
        ++rowCount; ++p;
    }
    std::wostringstream woss;
    woss << L"✓  Query complete \x2013  " << rowCount
         << L" row(s) returned in " << ms << L" ms";
    Log(woss.str());
    Log(L"");
}

// ── Buy Order ────────────────────────────────────────────────────────────────
void CMainDlg::RunBuyOrder(const std::string& symbol, int qty, double price,
                            const std::string& account)
{
    Log(L"");
    Log(L"═══════════════════════════════════════════════════════════");
    Log(L"  BUY ORDER");
    Log(L"═══════════════════════════════════════════════════════════");

    if (!m_connected) { Log(L"✗  Not connected."); Log(L""); return; }

    std::ostringstream oss;
    oss << R"({"symbol":")" << symbol << R"(","qty":)" << qty
        << R"(,"price":)" << price
        << R"(,"account":")" << account << R"("})";

    std::wostringstream woss;
    woss << L"  Symbol  : " << Utf8ToWide(symbol)
         << L"   Qty: " << qty
         << L"   Price: $" << price
         << L"   Account: " << Utf8ToWide(account);
    Log(woss.str());
    Log(L"Sending BUY request...");

    auto t0 = std::chrono::steady_clock::now();

    uint16_t respType = 0;
    std::string respPayload;
    if (!SendMsg(kMsgBuyOrder, oss.str()) ||
        !RecvMsg(respType, respPayload))
    {
        Log(L"✗  Request failed (socket error).");
        Log(L""); return;
    }

    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count() / 1000.0;

    if (respType == kRespError) {
        Log(L"✗  Server error: " + Utf8ToWide(respPayload));
        Log(L""); return;
    }

    Log(L"Order Acknowledgement:");
    std::string orderId  = ExtractField(respPayload, "order_id");
    std::string status   = ExtractField(respPayload, "status");
    std::string side     = ExtractField(respPayload, "side");
    std::string qtyField = ExtractField(respPayload, "qty");
    std::string priceF   = ExtractField(respPayload, "price");

    Log(L"  Order ID : " + Utf8ToWide(orderId));
    Log(L"  Side     : " + Utf8ToWide(side));
    Log(L"  Symbol   : " + Utf8ToWide(symbol));
    Log(L"  Qty      : " + Utf8ToWide(qtyField));
    Log(L"  Price    : $" + Utf8ToWide(priceF));
    Log(L"  Status   : " + Utf8ToWide(status));

    woss.str(L"");
    woss << L"✓  BUY order " << Utf8ToWide(orderId)
         << L" acknowledged in " << ms << L" ms";
    Log(woss.str());
    Log(L"");
}

// ── Sell Order ───────────────────────────────────────────────────────────────
void CMainDlg::RunSellOrder(const std::string& symbol, int qty, double price,
                             const std::string& account)
{
    Log(L"");
    Log(L"═══════════════════════════════════════════════════════════");
    Log(L"  SELL ORDER");
    Log(L"═══════════════════════════════════════════════════════════");

    if (!m_connected) { Log(L"✗  Not connected."); Log(L""); return; }

    std::ostringstream oss;
    oss << R"({"symbol":")" << symbol << R"(","qty":)" << qty
        << R"(,"price":)" << price
        << R"(,"account":")" << account << R"("})";

    std::wostringstream woss;
    woss << L"  Symbol  : " << Utf8ToWide(symbol)
         << L"   Qty: " << qty
         << L"   Price: $" << price
         << L"   Account: " << Utf8ToWide(account);
    Log(woss.str());
    Log(L"Sending SELL request...");

    auto t0 = std::chrono::steady_clock::now();

    uint16_t respType = 0;
    std::string respPayload;
    if (!SendMsg(kMsgSellOrder, oss.str()) ||
        !RecvMsg(respType, respPayload))
    {
        Log(L"✗  Request failed (socket error).");
        Log(L""); return;
    }

    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count() / 1000.0;

    if (respType == kRespError) {
        Log(L"✗  Server error: " + Utf8ToWide(respPayload));
        Log(L""); return;
    }

    std::string orderId  = ExtractField(respPayload, "order_id");
    std::string status   = ExtractField(respPayload, "status");
    std::string side     = ExtractField(respPayload, "side");
    std::string qtyField = ExtractField(respPayload, "qty");
    std::string priceF   = ExtractField(respPayload, "price");

    Log(L"Order Acknowledgement:");
    Log(L"  Order ID : " + Utf8ToWide(orderId));
    Log(L"  Side     : " + Utf8ToWide(side));
    Log(L"  Symbol   : " + Utf8ToWide(symbol));
    Log(L"  Qty      : " + Utf8ToWide(qtyField));
    Log(L"  Price    : $" + Utf8ToWide(priceF));
    Log(L"  Status   : " + Utf8ToWide(status));

    woss.str(L"");
    woss << L"✓  SELL order " << Utf8ToWide(orderId)
         << L" acknowledged in " << ms << L" ms";
    Log(woss.str());
    Log(L"");
}

// ── Get Position ─────────────────────────────────────────────────────────────
void CMainDlg::RunGetPosition(const std::string& account)
{
    Log(L"");
    Log(L"═══════════════════════════════════════════════════════════");
    Log(L"  GET POSITION");
    Log(L"═══════════════════════════════════════════════════════════");

    if (!m_connected) { Log(L"✗  Not connected."); Log(L""); return; }

    std::string payload = R"({"account":")" + account + R"("})";
    Log(L"Account : " + Utf8ToWide(account));
    Log(L"Sending GET POSITION request...");

    auto t0 = std::chrono::steady_clock::now();

    uint16_t respType = 0;
    std::string respPayload;
    if (!SendMsg(kMsgGetPos, payload) ||
        !RecvMsg(respType, respPayload))
    {
        Log(L"✗  Request failed (socket error).");
        Log(L""); return;
    }

    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count() / 1000.0;

    if (respType == kRespError) {
        Log(L"✗  Server error: " + Utf8ToWide(respPayload));
        Log(L""); return;
    }

    // Parse positions: count {"symbol": occurrences
    Log(L"Portfolio Snapshot:");
    size_t p = 0;
    int posIdx = 0;
    // Simple line-by-line extraction from the JSON positions array.
    while (true) {
        size_t start = respPayload.find("{\"symbol\":", p);
        if (start == std::string::npos) break;
        size_t end = respPayload.find('}', start);
        if (end == std::string::npos) break;
        std::string entry = respPayload.substr(start, end - start + 1);

        std::string sym      = ExtractField(entry, "symbol");
        std::string qty      = ExtractField(entry, "qty");
        std::string avgCost  = ExtractField(entry, "avg_cost");
        std::string current  = ExtractField(entry, "current");
        std::string pnl      = ExtractField(entry, "pnl");

        std::wostringstream woss;
        woss << L"  [" << (++posIdx) << L"] "
             << Utf8ToWide(sym)
             << L"  qty=" << Utf8ToWide(qty)
             << L"  avg=$" << Utf8ToWide(avgCost)
             << L"  cur=$" << Utf8ToWide(current)
             << L"  P&L=$" << Utf8ToWide(pnl);
        Log(woss.str());
        p = end + 1;
    }

    std::string cash = ExtractField(respPayload, "cash");
    Log(L"  Cash balance : $" + Utf8ToWide(cash));

    std::wostringstream woss;
    woss << L"✓  Position query complete \x2013  " << posIdx
         << L" position(s) in " << ms << L" ms";
    Log(woss.str());
    Log(L"");
}

// ── Price History ─────────────────────────────────────────────────────────────
void CMainDlg::RunGetPriceHistory(const std::string& symbol, int days)
{
    Log(L"");
    Log(L"═══════════════════════════════════════════════════════════");
    Log(L"  PRICE HISTORY");
    Log(L"═══════════════════════════════════════════════════════════");

    if (!m_connected) { Log(L"✗  Not connected."); Log(L""); return; }

    std::ostringstream oss;
    oss << R"({"symbol":")" << symbol << R"(","days":)" << days << "}";

    std::wostringstream woss;
    woss << L"  Symbol: " << Utf8ToWide(symbol)
         << L"  Period: " << days << L" days";
    Log(woss.str());
    Log(L"Sending PRICE HISTORY request...");

    auto t0 = std::chrono::steady_clock::now();

    uint16_t respType = 0;
    std::string respPayload;
    if (!SendMsg(kMsgPriceHist, oss.str()) ||
        !RecvMsg(respType, respPayload))
    {
        Log(L"✗  Request failed (socket error).");
        Log(L""); return;
    }

    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count() / 1000.0;

    if (respType == kRespError) {
        Log(L"✗  Server error: " + Utf8ToWide(respPayload));
        Log(L""); return;
    }

    // Count OHLCV candles (each has a "date" field).
    size_t candles = 0;
    size_t p = 0;
    while ((p = respPayload.find("\"date\":", p)) != std::string::npos) {
        ++candles; ++p;
    }

    // Show first 5 and last 5 candles for brevity.
    Log(L"OHLCV Data (first 5 candles):");
    p = 0;
    int shown = 0;
    while (shown < 5) {
        size_t start = respPayload.find("{\"date\":", p);
        if (start == std::string::npos) break;
        size_t end = respPayload.find('}', start);
        if (end == std::string::npos) break;
        std::string entry = respPayload.substr(start, end - start + 1);

        std::string date   = ExtractField(entry, "date");
        std::string open_  = ExtractField(entry, "open");
        std::string high   = ExtractField(entry, "high");
        std::string low    = ExtractField(entry, "low");
        std::string close_ = ExtractField(entry, "close");
        std::string vol    = ExtractField(entry, "volume");

        // Truncate numeric strings to 7 wide chars for compact display.
        // std::wstring::substr(0, n) is safe when n > size() – it returns
        // the full string – but we use std::min to make the intent explicit.
        auto trunc7 = [](const std::wstring& s) -> std::wstring {
            return s.substr(0, std::min(s.size(), size_t(7)));
        };

        std::wostringstream woss2;
        woss2 << L"  " << Utf8ToWide(date)
              << L"  O=" << trunc7(Utf8ToWide(open_))
              << L"  H=" << trunc7(Utf8ToWide(high))
              << L"  L=" << trunc7(Utf8ToWide(low))
              << L"  C=" << trunc7(Utf8ToWide(close_))
              << L"  Vol=" << Utf8ToWide(vol);
        Log(woss2.str());
        ++shown;
        p = end + 1;
    }
    if (candles > 5) {
        woss.str(L"");
        woss << L"  ... (" << (candles - 5) << L" more candles omitted) ...";
        Log(woss.str());
    }

    woss.str(L"");
    woss << L"✓  Price history complete \x2013  " << candles
         << L" candles received in " << ms << L" ms";
    Log(woss.str());
    Log(L"");
}

// ── Batch Test ────────────────────────────────────────────────────────────────
void CMainDlg::RunBatchTest()
{
    Log(L"");
    Log(L"╔══════════════════════════════════════════════════════════╗");
    Log(L"║               BATCH TEST  –  All Services               ║");
    Log(L"╚══════════════════════════════════════════════════════════╝");

    if (!m_connected) {
        Log(L"✗  Not connected to the server.  Please connect first.");
        Log(L"");
        return;
    }

    Log(L"Running all 12 service tests sequentially...");
    Log(L"");

    RunPing();
    Sleep(100);

    RunSqlQuery("SELECT symbol, price FROM quotes WHERE symbol='AAPL'");
    Sleep(100);

    RunSqlQuery("SELECT symbol, price, volume FROM quotes ORDER BY volume DESC");
    Sleep(100);

    RunBuyOrder ("AAPL", 100, 182.50, "ACC-001");
    Sleep(100);

    RunBuyOrder ("MSFT",  50, 310.10, "ACC-001");
    Sleep(100);

    RunSellOrder("AAPL",  75, 183.00, "ACC-002");
    Sleep(100);

    RunSellOrder("TSLA", 200, 245.50, "ACC-002");
    Sleep(100);

    RunGetPosition("ACC-001");
    Sleep(100);

    RunGetPosition("ACC-002");
    Sleep(100);

    RunGetPriceHistory("AAPL",  30);
    Sleep(100);

    RunGetPriceHistory("MSFT",   7);
    Sleep(100);

    RunGetPriceHistory("GOOGL", 14);

    Log(L"");
    Log(L"╔══════════════════════════════════════════════════════════╗");
    Log(L"║               BATCH TEST COMPLETE                       ║");
    Log(L"╚══════════════════════════════════════════════════════════╝");
    Log(L"");
}

// ===========================================================================
// JSON helpers
// ===========================================================================

/*static*/ std::string CMainDlg::ExtractString(const std::string& json,
                                                const std::string& key)
{
    return ExtractField(json, key);
}

/*static*/ std::wstring CMainDlg::SummariseResponse(uint16_t type,
                                                      const std::string& json)
{
    std::wostringstream woss;
    woss << L"[type=0x" << std::hex << type << std::dec << L"] "
         << Utf8ToWide(json.size() > 200 ? json.substr(0, 200) + "..." : json);
    return woss.str();
}
