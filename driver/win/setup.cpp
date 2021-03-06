#include "../platform.h"
#include "resource.h"

#if defined (_win_)

#include <algorithm>
#include <strsafe.h>

#include <odbcinst.h>
#pragma comment(lib, "odbc32.lib")
#pragma comment(lib, "legacy_stdio_definitions.lib")

#ifndef INTFUNC
#   define INTFUNC  __stdcall
#endif /* INTFUNC */

/// Saved module handle.
extern HINSTANCE module_instance;

namespace
{

#define LARGE_REGISTRY_LEN			4096		/* used for special cases */
#define MEDIUM_REGISTRY_LEN			256 /* normal size for * user,database,etc. */
#define SMALL_REGISTRY_LEN			10	/* for 1/0 settings */

#define MAXPGPATH	1024
/// Max keyword length
#define MAXKEYLEN	(32+1)
/// Max data source name length
#define	MAXDSNAME	(32+1)

#define INI_KDESC           TEXT("Description") /* Data source description */
#define INI_DATABASE        TEXT("Database")    /* Database Name */
#define INI_SERVER          TEXT("Server")      /* Name of Server  running the ClickHouse service */
#define INI_UID             TEXT("UID")         /* Default User Name */
#define INI_USERNAME        TEXT("Username")    /* Default User Name */
#define INI_PASSWORD        TEXT("Password")    /* Default Password */
#define INI_PORT            TEXT("Port")        /* Port on which the ClickHouse is listening */
#define INI_READONLY        TEXT("ReadOnly")    /* Database is read only */
#define INI_PROTOCOL        TEXT("Protocol")    /* What protocol (6.2) */
#define INI_DSN             TEXT("ClickHouse")

#define ABBR_PROTOCOL		TEXT("A1")
#define ABBR_READONLY		TEXT("A0")

#define SPEC_SERVER			TEXT("server")

#ifndef WIN32
#define ODBC_INI			".odbc.ini"
#define ODBCINST_INI		"odbcinst.ini"
#else
#define ODBC_INI			TEXT("ODBC.INI")
#define ODBCINST_INI		TEXT("ODBCINST.INI")

#endif

/*	Structure to hold all the connection attributes for a specific
connection (used for both registry and file, DSN and DRIVER)
*/
struct ConnInfo
{
    TCHAR		dsn[MEDIUM_REGISTRY_LEN];
    TCHAR		desc[MEDIUM_REGISTRY_LEN];
    TCHAR		drivername[MEDIUM_REGISTRY_LEN];
    TCHAR		server[MEDIUM_REGISTRY_LEN];
    TCHAR		database[MEDIUM_REGISTRY_LEN];
    TCHAR		username[MEDIUM_REGISTRY_LEN];
    TCHAR		password[MEDIUM_REGISTRY_LEN];
    TCHAR		port[SMALL_REGISTRY_LEN];
    TCHAR		sslmode[16];
    TCHAR		onlyread[SMALL_REGISTRY_LEN];
    TCHAR		fake_oid_index[SMALL_REGISTRY_LEN];
    TCHAR		show_oid_column[SMALL_REGISTRY_LEN];
    TCHAR		row_versioning[SMALL_REGISTRY_LEN];
    TCHAR		show_system_tables[SMALL_REGISTRY_LEN];
    TCHAR		translation_dll[MEDIUM_REGISTRY_LEN];
    TCHAR		translation_option[SMALL_REGISTRY_LEN];
    TCHAR		focus_password;
    TCHAR		conn_settings[MEDIUM_REGISTRY_LEN];
    signed char	disallow_premature = -1;
    signed char	allow_keyset = -1;
    signed char	updatable_cursors = 0;
    signed char	lf_conversion = -1;
    signed char	true_is_minus1 = -1;
    signed char	int8_as = -101;
    signed char	bytea_as_longvarbinary = -1;
    signed char	use_server_side_prepare = -1;
    signed char	lower_case_identifier = -1;
    signed char	rollback_on_error = -1;
    signed char	force_abbrev_connstr = -1;
    signed char	bde_environment = -1;
    signed char	fake_mss = -1;
    signed char	cvt_null_date_string = -1;
    signed char	autocommit_public = SQL_AUTOCOMMIT_ON;
    signed char	accessible_only = -1;
    signed char	ignore_round_trip_time = -1;
    signed char	disable_keepalive = -1;
    signed char	gssauth_use_gssapi = -1;
};

/* NOTE:  All these are used by the dialog procedures */
struct SetupDialogData
{
    HWND		hwnd_parent;	/// Parent window handle
    LPCTSTR		driver_name;	/// Driver description
    ConnInfo	ci;
    TCHAR		dsn[MAXDSNAME]; /// Original data source name
    bool		is_new_dsn;		/// New data source flag
    bool		is_default;	    /// Default data source flag
};

BOOL
copyAttributes(ConnInfo *ci, LPCTSTR attribute, LPCTSTR value)
{
    BOOL	found = TRUE;

    if (stricmp(attribute, TEXT("DSN")) == 0)
        strcpy(ci->dsn, value);

    else if (stricmp(attribute, TEXT("driver")) == 0)
        strcpy(ci->drivername, value);

    else if (stricmp(attribute, INI_KDESC) == 0)
        strcpy(ci->desc, value);

    else if (stricmp(attribute, INI_DATABASE) == 0)
        strcpy(ci->database, value);

    else if (stricmp(attribute, INI_SERVER) == 0 || stricmp(attribute, SPEC_SERVER) == 0)
        strcpy(ci->server, value);

    else if (stricmp(attribute, INI_USERNAME) == 0 || stricmp(attribute, INI_UID) == 0)
        strcpy(ci->username, value);

    //else if (stricmp(attribute, INI_PASSWORD) == 0 || stricmp(attribute, "pwd") == 0)
    //	ci->password = decode_or_remove_braces(value);

    else if (stricmp(attribute, INI_PORT) == 0)
        strcpy(ci->port, value);

    else if (stricmp(attribute, INI_READONLY) == 0 || stricmp(attribute, ABBR_READONLY) == 0)
        strcpy(ci->onlyread, value);
#if 0
    else if (stricmp(attribute, INI_PROTOCOL) == 0 || stricmp(attribute, ABBR_PROTOCOL) == 0)
    {
        char * ptr;
        /*
        * The first part of the Protocol used to be "6.2", "6.3" or
        * "7.4" to denote which protocol version to use. Nowadays we
        * only support the 7.4 protocol, also known as the protocol
        * version 3. So just ignore the first part of the string,
        * parsing only the rollback_on_error value.
        */
        ptr = (char*)strchr(value, '-');
        if (ptr)
        {
            if ('-' != *value)
            {
                *ptr = '\0';
                /* ignore first part */
            }
            ci->rollback_on_error = atoi(ptr + 1);
        }
    }

    else if (stricmp(attribute, INI_SHOWOIDCOLUMN) == 0 || stricmp(attribute, ABBR_SHOWOIDCOLUMN) == 0)
        strcpy(ci->show_oid_column, value);

    else if (stricmp(attribute, INI_FAKEOIDINDEX) == 0 || stricmp(attribute, ABBR_FAKEOIDINDEX) == 0)
        strcpy(ci->fake_oid_index, value);

    else if (stricmp(attribute, INI_ROWVERSIONING) == 0 || stricmp(attribute, ABBR_ROWVERSIONING) == 0)
        strcpy(ci->row_versioning, value);

    else if (stricmp(attribute, INI_SHOWSYSTEMTABLES) == 0 || stricmp(attribute, ABBR_SHOWSYSTEMTABLES) == 0)
        strcpy(ci->show_system_tables, value);

    else if (stricmp(attribute, INI_CONNSETTINGS) == 0 || stricmp(attribute, ABBR_CONNSETTINGS) == 0)
    {
        /* We can use the conn_settings directly when they are enclosed with braces */
        if ('{' == *value)
        {
            size_t	len;

            len = strlen(value + 1);
            if (len > 0 && '}' == value[len])
                len--;
            STRN_TO_NAME(ci->conn_settings, value + 1, len);
        }
        else
            ci->conn_settings = decode(value);
    }
    else if (stricmp(attribute, INI_DISALLOWPREMATURE) == 0 || stricmp(attribute, ABBR_DISALLOWPREMATURE) == 0)
        ci->disallow_premature = atoi(value);
    else if (stricmp(attribute, INI_UPDATABLECURSORS) == 0 || stricmp(attribute, ABBR_UPDATABLECURSORS) == 0)
        ci->allow_keyset = atoi(value);
    else if (stricmp(attribute, INI_LFCONVERSION) == 0 || stricmp(attribute, ABBR_LFCONVERSION) == 0)
        ci->lf_conversion = atoi(value);
    else if (stricmp(attribute, INI_TRUEISMINUS1) == 0 || stricmp(attribute, ABBR_TRUEISMINUS1) == 0)
        ci->true_is_minus1 = atoi(value);
    else if (stricmp(attribute, INI_INT8AS) == 0)
        ci->int8_as = atoi(value);
    else if (stricmp(attribute, INI_BYTEAASLONGVARBINARY) == 0 || stricmp(attribute, ABBR_BYTEAASLONGVARBINARY) == 0)
        ci->bytea_as_longvarbinary = atoi(value);
    else if (stricmp(attribute, INI_USESERVERSIDEPREPARE) == 0 || stricmp(attribute, ABBR_USESERVERSIDEPREPARE) == 0)
        ci->use_server_side_prepare = atoi(value);
    else if (stricmp(attribute, INI_LOWERCASEIDENTIFIER) == 0 || stricmp(attribute, ABBR_LOWERCASEIDENTIFIER) == 0)
        ci->lower_case_identifier = atoi(value);
    else if (stricmp(attribute, INI_GSSAUTHUSEGSSAPI) == 0 || stricmp(attribute, ABBR_GSSAUTHUSEGSSAPI) == 0)
        ci->gssauth_use_gssapi = atoi(value);
    else if (stricmp(attribute, INI_KEEPALIVETIME) == 0 || stricmp(attribute, ABBR_KEEPALIVETIME) == 0)
        ci->keepalive_idle = atoi(value);
    else if (stricmp(attribute, INI_KEEPALIVEINTERVAL) == 0 || stricmp(attribute, ABBR_KEEPALIVEINTERVAL) == 0)
        ci->keepalive_interval = atoi(value);
#ifdef	USE_LIBPQ
    else if (stricmp(attribute, INI_PREFERLIBPQ) == 0 || stricmp(attribute, ABBR_PREFERLIBPQ) == 0)
        ci->prefer_libpq = atoi(value);
#endif /* USE_LIBPQ */
    else if (stricmp(attribute, INI_SSLMODE) == 0 || stricmp(attribute, ABBR_SSLMODE) == 0)
    {
        switch (value[0])
        {
            case SSLLBYTE_ALLOW:
                strcpy(ci->sslmode, SSLMODE_ALLOW);
                break;
            case SSLLBYTE_PREFER:
                strcpy(ci->sslmode, SSLMODE_PREFER);
                break;
            case SSLLBYTE_REQUIRE:
                strcpy(ci->sslmode, SSLMODE_REQUIRE);
                break;
            case SSLLBYTE_VERIFY:
                switch (value[1])
                {
                    case 'f':
                        strcpy(ci->sslmode, SSLMODE_VERIFY_FULL);
                        break;
                    case 'c':
                        strcpy(ci->sslmode, SSLMODE_VERIFY_CA);
                        break;
                    default:
                        strcpy(ci->sslmode, value);
                }
                break;
            case SSLLBYTE_DISABLE:
            default:
                strcpy(ci->sslmode, SSLMODE_DISABLE);
                break;
        }
    }
    else if (stricmp(attribute, INI_ABBREVIATE) == 0)
        unfoldCXAttribute(ci, value);
#ifdef	_HANDLE_ENLIST_IN_DTC_
    else if (stricmp(attribute, INI_XAOPT) == 0)
        ci->xa_opt = atoi(value);
#endif /* _HANDLE_ENLIST_IN_DTC_ */
    else if (stricmp(attribute, INI_EXTRAOPTIONS) == 0)
    {
        UInt4	val1 = 0, val2 = 0;

        if ('+' == value[0])
        {
            sscanf(value + 1, "%x-%x", &val1, &val2);
            add_removeExtraOptions(ci, val1, val2);
        }
        else if ('-' == value[0])
        {
            sscanf(value + 1, "%x", &val2);
            add_removeExtraOptions(ci, 0, val2);
        }
        else
        {
            setExtraOptions(ci, value, hex_format);
        }
    }
#endif
    else
        found = FALSE;

    return found;
}

static void parseAttributes(LPCTSTR lpszAttributes, SetupDialogData * lpsetupdlg)
{
    LPCTSTR		lpsz;
    LPCTSTR		lpszStart;
    TCHAR		aszKey[MAXKEYLEN];
    int			cbKey;
    TCHAR		value[MAXPGPATH];

    //CC_conninfo_init(&(lpsetupdlg->ci), COPY_GLOBALS);

    for (lpsz = lpszAttributes; *lpsz; lpsz++)
    {
        /*
        * Extract key name (e.g., DSN), it must be terminated by an
        * equals
        */
        lpszStart = lpsz;
        for (;; lpsz++)
        {
            if (!*lpsz)
                return;			/* No key was found */
            else if (*lpsz == '=')
                break;			/* Valid key found */
        }
        /* Determine the key's index in the key table (-1 if not found) */
        cbKey = lpsz - lpszStart;
        if (cbKey < sizeof(aszKey))
        {
            memcpy(aszKey, lpszStart, cbKey * sizeof(TCHAR));
            aszKey[cbKey] = '\0';
        }

        /* Locate end of key value */
        lpszStart = ++lpsz;
        for (; *lpsz; lpsz++)
            ;

        /* lpsetupdlg->aAttr[iElement].fSupplied = TRUE; */
        memcpy(value, lpszStart, std::min<size_t>(lpsz - lpszStart + 1, MAXPGPATH) * sizeof(TCHAR));

        /* Copy the appropriate value to the conninfo  */
        copyAttributes(&lpsetupdlg->ci, aszKey, value);

        //if (!copyAttributes(&lpsetupdlg->ci, aszKey, value))
        //    copyCommonAttributes(&lpsetupdlg->ci, aszKey, value);
    }
}

void getDSNinfo(ConnInfo *ci, bool overwrite)
{
    LPCTSTR DSN = ci->dsn;

    if (ci->desc[0] == '\0' || overwrite)
        SQLGetPrivateProfileString(DSN, INI_KDESC, TEXT(""), ci->desc, sizeof(ci->desc), ODBC_INI);

    if (ci->server[0] == '\0' || overwrite)
        SQLGetPrivateProfileString(DSN, INI_SERVER, TEXT(""), ci->server, sizeof(ci->server), ODBC_INI);

    if (ci->database[0] == '\0' || overwrite)
        SQLGetPrivateProfileString(DSN, INI_DATABASE, TEXT(""), ci->database, sizeof(ci->database), ODBC_INI);

    if (ci->username[0] == '\0' || overwrite)
        SQLGetPrivateProfileString(DSN, INI_USERNAME, TEXT(""), ci->username, sizeof(ci->username), ODBC_INI);

    if (ci->port[0] == '\0' || overwrite)
        SQLGetPrivateProfileString(DSN, INI_PORT, TEXT(""), ci->port, sizeof(ci->port), ODBC_INI);

    if (ci->onlyread[0] == '\0' || overwrite)
        SQLGetPrivateProfileString(DSN, INI_READONLY, TEXT(""), ci->onlyread, sizeof(ci->onlyread), ODBC_INI);
}

/*	This is for datasource based options only */
void writeDSNinfo(const ConnInfo * ci)
{
    const LPCTSTR DSN = ci->dsn;
    //char encoded_item[LARGE_REGISTRY_LEN];
    //char temp[SMALL_REGISTRY_LEN];

    SQLWritePrivateProfileString(DSN,
                                 INI_KDESC,
                                 ci->desc,
                                 ODBC_INI);

    SQLWritePrivateProfileString(DSN,
                                 INI_DATABASE,
                                 ci->database,
                                 ODBC_INI);

    SQLWritePrivateProfileString(DSN,
                                 INI_SERVER,
                                 ci->server,
                                 ODBC_INI);

    SQLWritePrivateProfileString(DSN,
                                 INI_PORT,
                                 ci->port,
                                 ODBC_INI);

    SQLWritePrivateProfileString(DSN,
                                 INI_USERNAME,
                                 ci->username,
                                 ODBC_INI);
    SQLWritePrivateProfileString(DSN, INI_UID, ci->username, ODBC_INI);

    SQLWritePrivateProfileString(DSN,
                                 INI_READONLY,
                                 ci->onlyread,
                                 ODBC_INI);
}

static bool setDSNAttributes(HWND hwndParent, SetupDialogData * lpsetupdlg, DWORD * errcode)
{
    /// Pointer to data source name
    LPCTSTR lpszDSN = lpsetupdlg->ci.dsn;

    if (errcode)
        *errcode = 0;

    /* Validate arguments */
    if (lpsetupdlg->is_new_dsn && !*lpsetupdlg->ci.dsn)
        return FALSE;
    if (!SQLValidDSN(lpszDSN))
        return FALSE;

    /* Write the data source name */
    if (!SQLWriteDSNToIni(lpszDSN, lpsetupdlg->driver_name))
    {
        RETCODE	ret = SQL_ERROR;
        DWORD	err = SQL_ERROR;
        TCHAR   szMsg[SQL_MAX_MESSAGE_LENGTH];

        ret = SQLInstallerError(1, &err, szMsg, sizeof(szMsg), NULL);
        if (hwndParent)
        {
            if (SQL_SUCCESS != ret)
                MessageBox(hwndParent, szMsg, TEXT("Bad DSN configuration"), MB_ICONEXCLAMATION | MB_OK);
        }
        if (errcode)
            *errcode = err;
        return FALSE;
    }

    /* Update ODBC.INI */
    //writeDriverCommoninfo(ODBC_INI, lpsetupdlg->ci.dsn, &(lpsetupdlg->ci.drivers));
    writeDSNinfo(&lpsetupdlg->ci);

    /* If the data source name has changed, remove the old name */
    if (lstrcmpi(lpsetupdlg->dsn, lpsetupdlg->ci.dsn))
        SQLRemoveDSNFromIni(lpsetupdlg->dsn);

    return TRUE;
}

} // namespace

extern "C"
{

void INTFUNC
    CenterDialog(HWND hdlg)
{
    HWND    hwndFrame;
    RECT    rcDlg;
    RECT    rcScr;
    RECT    rcFrame;
    int	    cx;
    int     cy;

    hwndFrame = GetParent(hdlg);

    GetWindowRect(hdlg, &rcDlg);
    cx = rcDlg.right - rcDlg.left;
    cy = rcDlg.bottom - rcDlg.top;

    GetClientRect(hwndFrame, &rcFrame);
    ClientToScreen(hwndFrame, (LPPOINT)(&rcFrame.left));
    ClientToScreen(hwndFrame, (LPPOINT)(&rcFrame.right));
    rcDlg.top = rcFrame.top + (((rcFrame.bottom - rcFrame.top) - cy) >> 1);
    rcDlg.left = rcFrame.left + (((rcFrame.right - rcFrame.left) - cx) >> 1);
    rcDlg.bottom = rcDlg.top + cy;
    rcDlg.right = rcDlg.left + cx;

    GetWindowRect(GetDesktopWindow(), &rcScr);
    if (rcDlg.bottom > rcScr.bottom)
    {
        rcDlg.bottom = rcScr.bottom;
        rcDlg.top = rcDlg.bottom - cy;
    }
    if (rcDlg.right > rcScr.right)
    {
        rcDlg.right = rcScr.right;
        rcDlg.left = rcDlg.right - cx;
    }

    if (rcDlg.left < 0)
        rcDlg.left = 0;
    if (rcDlg.top < 0)
        rcDlg.top = 0;

    MoveWindow(hdlg, rcDlg.left, rcDlg.top, cx, cy, TRUE);
    return;
}

INT_PTR	CALLBACK
    ConfigDlgProc(HWND hdlg,
                    UINT wMsg,
                    WPARAM wParam,
                    LPARAM lParam)
{
    SetupDialogData * lpsetupdlg;
    ConnInfo * ci;
    //char strbuf[64];

    switch (wMsg)
    {
        case WM_INITDIALOG:
        {
            lpsetupdlg = (SetupDialogData *)lParam;
            ci = &lpsetupdlg->ci;
            SetWindowLongPtr(hdlg, DWLP_USER, lParam);

            CenterDialog(hdlg); /* Center dialog */

            getDSNinfo(ci, false);

            SetDlgItemText(hdlg, IDC_DSN_NAME, ci->dsn);
            SetDlgItemText(hdlg, IDC_DESCRIPTION, ci->desc);
            SetDlgItemText(hdlg, IDC_SERVER_HOST, ci->server);
            SetDlgItemText(hdlg, IDC_SERVER_PORT, ci->port);
            SetDlgItemText(hdlg, IDC_DATABASE, ci->database);
            SetDlgItemText(hdlg, IDC_USER, ci->username);
            SetDlgItemText(hdlg, IDC_PASSWORD, ci->password);

            return TRUE;		/* Focus was not set */
        }

        case WM_COMMAND:
            switch (const DWORD cmd = LOWORD(wParam))
            {
                case IDOK:
                    lpsetupdlg = (SetupDialogData *)GetWindowLongPtr(hdlg, DWLP_USER);
                    ci = &lpsetupdlg->ci;

                    GetDlgItemText(hdlg, IDC_DSN_NAME, ci->dsn, sizeof(ci->dsn));
                    GetDlgItemText(hdlg, IDC_DESCRIPTION, ci->desc, sizeof(ci->desc));
                    GetDlgItemText(hdlg, IDC_SERVER_HOST, ci->server, sizeof(ci->server));
                    GetDlgItemText(hdlg, IDC_SERVER_PORT, ci->port, sizeof(ci->port));
                    GetDlgItemText(hdlg, IDC_DATABASE, ci->database, sizeof(ci->database));
                    GetDlgItemText(hdlg, IDC_USER, ci->username, sizeof(ci->username));
                    GetDlgItemText(hdlg, IDC_PASSWORD, ci->password, sizeof(ci->password));

                    /* Return to caller */
                case IDCANCEL:
                    EndDialog(hdlg, cmd);
                    return TRUE;
            }
            break;
    }

    /* Message not processed */
    return FALSE;
}

BOOL CALLBACK
#ifdef UNICODE
ConfigDSNW(
#else
ConfigDSN(
#endif
    HWND hwnd,
    WORD fRequest,
    LPCTSTR lpszDriver,
    LPCTSTR lpszAttributes)
{
    BOOL fSuccess = FALSE;
    GLOBALHANDLE hglbAttr;
    SetupDialogData * lpsetupdlg;

    /* Allocate attribute array */
    hglbAttr = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(SetupDialogData));
    if (!hglbAttr)
        return FALSE;
    lpsetupdlg = (SetupDialogData *)GlobalLock(hglbAttr);
    /* Parse attribute string */
    if (lpszAttributes)
        parseAttributes(lpszAttributes, lpsetupdlg);

    /* Save original data source name */
    if (lpsetupdlg->ci.dsn[0])
        lstrcpy(lpsetupdlg->dsn, lpsetupdlg->ci.dsn);
    else
        lpsetupdlg->dsn[0] = '\0';

    /* Remove data source */
    if (ODBC_REMOVE_DSN == fRequest)
    {
        /* Fail if no data source name was supplied */
        if (!lpsetupdlg->ci.dsn[0])
            fSuccess = FALSE;

        /* Otherwise remove data source from ODBC.INI */
        else
            fSuccess = SQLRemoveDSNFromIni(lpsetupdlg->ci.dsn);
    }
    /* Add or Configure data source */
    else
    {
        /* Save passed variables for global access (e.g., dialog access) */
        lpsetupdlg->hwnd_parent = hwnd;
        lpsetupdlg->driver_name = lpszDriver;
        lpsetupdlg->is_new_dsn = (ODBC_ADD_DSN == fRequest);
        lpsetupdlg->is_default = !lstrcmpi(lpsetupdlg->ci.dsn, INI_DSN);

        /*
        * Display the appropriate dialog (if parent window handle
        * supplied)
        */
        if (hwnd)
        {
            /* Display dialog(s) */
            auto ret = DialogBoxParam(module_instance,
                                      MAKEINTRESOURCE(IDD_DIALOG1),
                                      hwnd,
                                      ConfigDlgProc,
                                      (LPARAM)lpsetupdlg);
            if (ret == IDOK)
            {
                fSuccess = setDSNAttributes(hwnd, lpsetupdlg, NULL);
            }
            else if (ret != IDCANCEL)
            {
                auto err = GetLastError();
                LPVOID lpMsgBuf;
                LPVOID lpDisplayBuf;

                FormatMessage(
                    FORMAT_MESSAGE_ALLOCATE_BUFFER |
                    FORMAT_MESSAGE_FROM_SYSTEM |
                    FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL,
                    err,
                    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                    (LPTSTR)&lpMsgBuf,
                    0, NULL);

                lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
                    (lstrlen((LPCTSTR)lpMsgBuf) + 40) * sizeof(TCHAR));
                StringCchPrintf((LPTSTR)lpDisplayBuf,
                                LocalSize(lpDisplayBuf) / sizeof(TCHAR),
                                TEXT("failed with error %d: %s"),
                                err, lpMsgBuf);
                MessageBox(NULL, (LPCTSTR)lpDisplayBuf, TEXT("Error"), MB_OK);

                LocalFree(lpMsgBuf);
                LocalFree(lpDisplayBuf);
            }
        }
        else if (lpsetupdlg->ci.dsn[0])
            fSuccess = setDSNAttributes(hwnd, lpsetupdlg, NULL);
        else
            fSuccess = TRUE;
    }

    GlobalUnlock(hglbAttr);
    GlobalFree(hglbAttr);

    return fSuccess;
}

BOOL CALLBACK
#ifdef UNICODE
ConfigDriverW(
#else
ConfigDriver(
#endif
    HWND hwnd,
    WORD fRequest,
    LPCTSTR lpszDriver,
    LPCTSTR lpszArgs,
    LPTSTR lpszMsg,
    WORD cbMsgMax,
    WORD * pcbMsgOut)
{
    MessageBox(hwnd, TEXT("ConfigDriver"), TEXT("Debug"), MB_OK);
    return TRUE;
}

} // extern

#endif
