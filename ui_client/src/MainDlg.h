// MainDlg.h
// Declaration of the main dialog class for the HS Financial Services
// MFC client test application.
//
// The dialog exposes:
//   • A Connection group  – host / port entry, Connect / Disconnect buttons,
//                           and a live status indicator.
//   • A Command group     – drop-down list of all testable server commands,
//                           plus a "Run" button.
//   • An Output group     – scrollable, read-only, multi-line edit box that
//                           receives timestamped log lines, plus a "Clear" button.
//
// All network I/O runs on a background std::thread so the UI stays responsive.
// The worker thread uses PostMessage(WM_LOG_LINE) to push text back to the
// UI thread.

#pragma once

#include "stdafx.h"
#include "../res/resource.h"

// ---------------------------------------------------------------------------
// Custom window messages
// ---------------------------------------------------------------------------

/// Posted by the worker thread to append a line to the output edit box.
/// wParam = unused, lParam = pointer to heap-allocated std::wstring (caller
/// transfers ownership; the handler deletes it).
#define WM_LOG_LINE     (WM_USER + 1)

/// Posted by the worker thread when an asynchronous operation finishes so that
/// the "Run" button can be re-enabled.
#define WM_WORKER_DONE  (WM_USER + 2)

// ---------------------------------------------------------------------------
// Command descriptor
// ---------------------------------------------------------------------------

struct CommandEntry {
    const wchar_t* label;        ///< Text shown in the combo box
    int            id;           ///< Internal command identifier (see enum below)
};

enum class CmdId {
    Login = 0,
    Logout,
    Ping,
    SqlQueryAapl,
    SqlQueryMsft,
    SqlQueryAll,
    BuyAapl,
    BuyMsft,
    SellAapl,
    SellTsla,
    GetPositionAcc001,
    GetPositionAcc002,
    PriceHistAapl30,
    PriceHistMsft7,
    PriceHistGoogl14,
    BatchTest,
};

// ---------------------------------------------------------------------------
// CMainDlg
// ---------------------------------------------------------------------------

class CMainDlg : public CDialogEx {
public:
    explicit CMainDlg(CWnd* pParent = nullptr);

    enum { IDD = IDD_MAIN_DIALOG };

protected:
    // ── CDialog overrides ───────────────────────────────────────────────────
    virtual void DoDataExchange(CDataExchange* pDX) override;
    virtual BOOL OnInitDialog() override;
    virtual void OnOK()    override {}   // Disable Enter closing the dialog.
    virtual void OnCancel() override;

    // ── Button handlers ─────────────────────────────────────────────────────
    afx_msg void OnBnClickedConnect();
    afx_msg void OnBnClickedDisconnect();
    afx_msg void OnBnClickedRun();
    afx_msg void OnBnClickedClear();

    // ── Worker thread messages ──────────────────────────────────────────────
    afx_msg LRESULT OnLogLine(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnWorkerDone(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

private:
    // ── UI controls ─────────────────────────────────────────────────────────
    CEdit      m_editHost;
    CEdit      m_editPort;
    CComboBox  m_comboCommands;
    CEdit      m_editOutput;
    CStatic    m_staticStatus;

    // ── Connection state ────────────────────────────────────────────────────
    SOCKET     m_socket    = INVALID_SOCKET;
    bool       m_connected = false;

    // ── Worker thread ────────────────────────────────────────────────────────
    std::thread        m_workerThread;
    std::atomic<bool>  m_workerRunning{false};

    // ── Helpers: logging ────────────────────────────────────────────────────

    /// Append a UTF-16 line with a timestamp prefix to the output box.
    /// Thread-safe: may be called from any thread.
    void Log(const std::wstring& line);

    /// Same as Log() but accepts a narrow string (converted to UTF-16).
    void LogA(const std::string& line);

    /// Convenience overload for wide string literals.
    void Log(const wchar_t* line) { Log(std::wstring(line)); }

    /// Return the current local time as L"[HH:MM:SS]".
    static std::wstring Timestamp();

    // ── Helpers: connection ─────────────────────────────────────────────────

    /// Establish a TCP connection.  Returns true on success.
    bool DoConnect(const std::string& host, uint16_t port);

    /// Close and invalidate the socket.
    void DoDisconnect();

    /// Update the status label and its colour dot.
    void SetStatus(const CString& text, COLORREF dotColour);

    // ── Helpers: protocol ───────────────────────────────────────────────────

    /// Send a framed message over m_socket.  Returns false on error.
    bool SendMsg(uint16_t type, const std::string& payload);

    /// Receive a framed message from m_socket.  Returns false on error.
    bool RecvMsg(uint16_t& outType, std::string& outPayload);

    /// Reliable send: loops until all bytes are written.
    bool SendAll(const uint8_t* data, size_t len);

    /// Reliable recv: loops until exactly `len` bytes are read.
    bool RecvAll(uint8_t* data, size_t len);

    // ── Helpers: command runners (run on worker thread) ──────────────────────
    void RunLogin();
    void RunLogout();
    void RunPing();
    void RunSqlQuery(const std::string& sql);
    void RunBuyOrder(const std::string& symbol, int qty, double price,
                     const std::string& account);
    void RunSellOrder(const std::string& symbol, int qty, double price,
                      const std::string& account);
    void RunGetPosition(const std::string& account);
    void RunGetPriceHistory(const std::string& symbol, int days);
    void RunBatchTest();

    // ── Helpers: JSON parsing ────────────────────────────────────────────────
    /// Extract the value of a string field from a minimal JSON object.
    static std::string ExtractString(const std::string& json,
                                     const std::string& key);

    /// Pretty-print / summarise a JSON response for the output box.
    static std::wstring SummariseResponse(uint16_t type,
                                          const std::string& json);

    /// Enable / disable the Run button.
    void SetRunEnabled(bool enabled);
};
