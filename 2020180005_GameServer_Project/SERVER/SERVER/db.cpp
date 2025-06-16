#include "db.h"

void print_odbc_error(SQLHANDLE hHandle, SQLSMALLINT hType, RETCODE retcode)
{
    SQLSMALLINT i_rec = 0;
    SQLINTEGER i_error;
    WCHAR wsz_message[1000];
    WCHAR wsz_state[SQL_SQLSTATE_SIZE + 1];

    if (retcode == SQL_INVALID_HANDLE)
    {
        fwprintf(stderr, L" ODBC INVALID HANDLE \n");
        return;
    }

    while (SQLGetDiagRec(hType, hHandle, ++i_rec, wsz_state, &i_error, wsz_message, (SQLSMALLINT)(sizeof(wsz_message) / sizeof(WCHAR)), NULL) == SQL_SUCCESS)
    {
        if (wcsncmp(wsz_state, L"01004", 5))
        {
            fwprintf(stderr, L"[ODBC][%5.5s] %s (%d)\n", wsz_state, wsz_message, i_error);
        }
    }
}

bool DB_Initialize(const wchar_t* dsn_name)
{
    RETCODE retcode;

    setlocale(LC_ALL, "korean");

    retcode = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &g_hEnv);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        fprintf(stderr, "SQLAllocHandle ENV FAIL\n");
        return false;
    }

    retcode = SQLSetEnvAttr(g_hEnv, SQL_ATTR_ODBC_VERSION, (SQLPOINTER*)SQL_OV_ODBC3, 0);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        fprintf(stderr, " SQLSetEnvAttr FAIL \n");
        print_odbc_error(g_hEnv, SQL_HANDLE_ENV, retcode);
        return false;
    }

    retcode = SQLAllocHandle(SQL_HANDLE_DBC, g_hEnv, &g_hDbc);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        fprintf(stderr, " SQLAllocHandle DBC FAIL \n");
        print_odbc_error(g_hEnv, SQL_HANDLE_ENV, retcode);
        return false;
    }

    retcode = SQLConnectW(g_hDbc, (SQLWCHAR*)dsn_name, SQL_NTS, nullptr, 0, nullptr, 0);

    if (retcode != SQL_SUCCESS && retcode != SQL_SUCCESS_WITH_INFO)
    {
        fprintf(stderr, " SQLConnect FAIL DSN=%ls \n", dsn_name);
        print_odbc_error(g_hDbc, SQL_HANDLE_DBC, retcode);
        return false;
    }

    wprintf(L" DSN=%ls CONNECT COMPLETE \n", dsn_name);

    return true;
}

void DB_Cleanup()
{
    if (g_hDbc != SQL_NULL_HDBC)
    {
        SQLDisconnect(g_hDbc);
        SQLFreeHandle(SQL_HANDLE_DBC, g_hDbc);
        g_hDbc = SQL_NULL_HDBC;
    }

    if (g_hEnv != SQL_NULL_HENV)
    {
        SQLFreeHandle(SQL_HANDLE_ENV, g_hEnv);
        g_hEnv = SQL_NULL_HENV;
    }
}

bool DB_LoadPlayerPosition(const wchar_t* user_id, int& out_x, int& out_y, int& out_hp, int& out_exp,
    int& out_level, int& out_potion, int& out_exppotion, int& out_gold)
{
    std::lock_guard<std::mutex> lk(db_mutex);
    SQLHSTMT h_stmt = SQL_NULL_HSTMT;
    RETCODE rc;

    rc = SQLAllocHandle(SQL_HANDLE_STMT, g_hDbc, &h_stmt);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        return false;
    }


    SQLWCHAR* sql = (SQLWCHAR*)L"{ CALL dbo.sp_LoadPlayerPosition(?, ?, ?, ?, ?, ?, ?, ?, ?) }";
    rc = SQLPrepareW(h_stmt, sql, SQL_NTS);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        goto CLEANUP;
    }

    SQLBindParameter(h_stmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        10, 0, (SQLPOINTER)user_id, 0, NULL);
    SQLBindParameter(h_stmt, 2, SQL_PARAM_OUTPUT, SQL_C_SHORT, SQL_SMALLINT,
        0, 0, &out_x, 0, NULL);
    SQLBindParameter(h_stmt, 3, SQL_PARAM_OUTPUT, SQL_C_SHORT, SQL_SMALLINT,
        0, 0, &out_y, 0, NULL);
    SQLBindParameter(h_stmt, 4, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &out_hp, 0, NULL);
    SQLBindParameter(h_stmt, 5, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &out_exp, 0, NULL);
    SQLBindParameter(h_stmt, 6, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &out_level, 0, NULL);
    SQLBindParameter(h_stmt, 7, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &out_potion, 0, NULL);
    SQLBindParameter(h_stmt, 8, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &out_exppotion, 0, NULL);
    SQLBindParameter(h_stmt, 9, SQL_PARAM_OUTPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &out_gold, 0, NULL);


    rc = SQLExecute(h_stmt);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        goto CLEANUP;
    }

    cout << "DB_LoadPlayerPosition COMPLETE\n";
    SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);
    return true;

CLEANUP:
    cout << "DB_LoadPlayerPosition ERROR\n";
    SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);
    return false;
}

bool DB_InsertPlayerPosition(const wchar_t* user_id, short x, short y, int hp, int exp, int level, int potion, int exp_potion, int gold)
{
    std::lock_guard<std::mutex> lk(db_mutex);
    SQLHSTMT h_stmt = SQL_NULL_HSTMT;
    RETCODE rc;

    rc = SQLAllocHandle(SQL_HANDLE_STMT, g_hDbc, &h_stmt);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        return false;
    }


    static const SQLWCHAR* sql = L"{ CALL dbo.sp_InsertPlayerPosition(?, ?, ?, ?, ?, ?, ?, ?, ?) }";
    rc = SQLPrepareW(h_stmt, const_cast<SQLWCHAR*>(sql), SQL_NTS);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        goto CLEANUP;
    }

    SQLBindParameter(h_stmt, 1, SQL_PARAM_INPUT, SQL_C_WCHAR, SQL_WVARCHAR,
        10, 0, (SQLPOINTER)user_id, 0, nullptr);
    SQLBindParameter(h_stmt, 2, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_SMALLINT,
        0, 0, &x, 0, nullptr);
    SQLBindParameter(h_stmt, 3, SQL_PARAM_INPUT, SQL_C_SHORT, SQL_SMALLINT,
        0, 0, &y, 0, nullptr);
    SQLBindParameter(h_stmt, 4, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &hp, 0, nullptr);
    SQLBindParameter(h_stmt, 5, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &exp, 0, nullptr);
    SQLBindParameter(h_stmt, 6, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &level, 0, nullptr);
    SQLBindParameter(h_stmt, 7, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &potion, 0, nullptr);
    SQLBindParameter(h_stmt, 8, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &exp_potion, 0, nullptr);
    SQLBindParameter(h_stmt, 9, SQL_PARAM_INPUT, SQL_C_LONG, SQL_INTEGER,
        0, 0, &gold, 0, nullptr);

    rc = SQLExecute(h_stmt);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        goto CLEANUP;
    }

    cout << "DB_InsertPlayerPosition COMPLETE\n";
    SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);
    return true;

CLEANUP:
    cout << "DB_InsertPlayerPosition ERROR\n";
    SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);
    return false;
}

bool DB_SavePlayerPosition(const wchar_t* user_id, int x, int y, int hp, int exp, int level, int potion, int exp_potion, int gold)
{
    std::lock_guard<std::mutex> lk(db_mutex);

    if (g_hDbc == SQL_NULL_HDBC)
    {
        return false;
    }

    SQLHSTMT h_stmt = SQL_NULL_HSTMT;
    RETCODE rc;

    rc = SQLAllocHandle(SQL_HANDLE_STMT, g_hDbc, &h_stmt);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        print_odbc_error(g_hDbc, SQL_HANDLE_DBC, rc);
        return false;
    }

    static const SQLWCHAR* sql = L"{ CALL dbo.sp_SavePlayerPosition(?, ?, ?, ?, ?, ?, ?, ?, ?) }";
    rc = SQLPrepareW(h_stmt, const_cast<SQLWCHAR*>(sql), SQL_NTS);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        print_odbc_error(h_stmt, SQL_HANDLE_STMT, rc);
        SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);
        cout << "DB_SavePlayerPosition ERROR \n";

        return false;
    }

    auto bindParam = [&](SQLUSMALLINT idx, SQLSMALLINT cType, SQLSMALLINT sqlType, SQLULEN colSize, SQLPOINTER buf, SQLLEN buflen)

    {
        RETCODE r = SQLBindParameter(h_stmt, idx, SQL_PARAM_INPUT, cType, sqlType, colSize,
            0, buf, buflen, nullptr);

        if (r != SQL_SUCCESS && r != SQL_SUCCESS_WITH_INFO)
        {
            print_odbc_error(h_stmt, SQL_HANDLE_STMT, r);
            SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);

            throw std::runtime_error("BIND FAILED");
        }
    };

    SQLLEN idBufLen = (SQLLEN)((wcslen(user_id) + 1) * sizeof(wchar_t));
    bindParam(1, SQL_C_WCHAR, SQL_WVARCHAR, 10, (SQLPOINTER)user_id, idBufLen);
    bindParam(2, SQL_C_SLONG, SQL_INTEGER, 0, &x, 0);
    bindParam(3, SQL_C_SLONG, SQL_INTEGER, 0, &y, 0);
    bindParam(4, SQL_C_SLONG, SQL_INTEGER, 0, &hp, 0);
    bindParam(5, SQL_C_SLONG, SQL_INTEGER, 0, &exp, 0);
    bindParam(6, SQL_C_SLONG, SQL_INTEGER, 0, &level, 0);
    bindParam(7, SQL_C_SLONG, SQL_INTEGER, 0, &potion, 0);
    bindParam(8, SQL_C_SLONG, SQL_INTEGER, 0, &exp_potion, 0);
    bindParam(9, SQL_C_SLONG, SQL_INTEGER, 0, &gold, 0);


    rc = SQLExecute(h_stmt);

    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
    {
        print_odbc_error(h_stmt, SQL_HANDLE_STMT, rc);
        SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);
        cout << "DB_SavePlayerPosition ERROR \n";

        return false;
    }

    cout << "DB_SavePlayerPosition COMPLETE \n";
    SQLFreeHandle(SQL_HANDLE_STMT, h_stmt);

    return true;
}