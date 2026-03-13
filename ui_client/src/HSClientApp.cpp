// HSClientApp.cpp
// Implementation of the CWinApp-derived application class.
//
// Responsibilities:
//   * Initialise Winsock 2.
//   * Display the main dialog as the application's top-level window.
//   * Clean up Winsock on exit.

#include "stdafx.h"
#include "HSClientApp.h"

// ---------------------------------------------------------------------------
// The one global application object.
// ---------------------------------------------------------------------------
CHSClientApp theApp;

// ---------------------------------------------------------------------------
// Message map
// ---------------------------------------------------------------------------
BEGIN_MESSAGE_MAP(CHSClientApp, CWinApp)
END_MESSAGE_MAP()

// ---------------------------------------------------------------------------
CHSClientApp::CHSClientApp()
    : CWinApp()
{
}

// ---------------------------------------------------------------------------
BOOL CHSClientApp::InitInstance()
{
    // Initialise MFC common controls (required for visual styles).
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_WIN95_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    CWinApp::InitInstance();

    // ── Winsock initialisation ──────────────────────────────────────────────
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        AfxMessageBox(_T("Failed to initialise Winsock 2."), MB_OK | MB_ICONERROR);
        return FALSE;
    }

    // ── Show the main dialog ────────────────────────────────────────────────
    CMainDlg dlg;
    m_pMainWnd = &dlg;
    dlg.DoModal();

    // ── Winsock cleanup ─────────────────────────────────────────────────────
    WSACleanup();

    return FALSE; // Do not enter the standard message pump.
}
