// HSClientApp.h
// Declaration of the CWinApp-derived application class.

#pragma once

#include "stdafx.h"
#include "MainDlg.h"

// ---------------------------------------------------------------------------
// CHSClientApp
// ---------------------------------------------------------------------------

class CHSClientApp : public CWinApp {
public:
    CHSClientApp();

    // CWinApp overrides
    virtual BOOL InitInstance() override;

    DECLARE_MESSAGE_MAP()
};
