
#ifndef __WIL_TOKEN_HELPERS_INCLUDED
#define __WIL_TOKEN_HELPERS_INCLUDED

#include "resource.h"
#include <new>
#include <lmcons.h>         // for UNLEN and DNLEN
#include <processthreadsapi.h>

// for GetUserNameEx()
#define SECURITY_WIN32
#include <Security.h>

namespace wil
{
    /// @cond
    namespace details
    {
        // Template specialization for TOKEN_INFORMATION_CLASS, add more mappings here as needed
        // TODO: The mapping should be reversed to be MapTokenInfoClassToStruct since there may
        // be an info class value that uses the same structure. That is the case for the file
        // system information.
        template <typename T> struct MapTokenStructToInfoClass;
        template <> struct MapTokenStructToInfoClass<TOKEN_USER> { static const TOKEN_INFORMATION_CLASS infoClass = TokenUser; };
        template <> struct MapTokenStructToInfoClass<TOKEN_PRIVILEGES> { static const TOKEN_INFORMATION_CLASS infoClass = TokenPrivileges; };
        template <> struct MapTokenStructToInfoClass<TOKEN_OWNER> { static const TOKEN_INFORMATION_CLASS infoClass = TokenOwner; };
        template <> struct MapTokenStructToInfoClass<TOKEN_PRIMARY_GROUP> { static const TOKEN_INFORMATION_CLASS infoClass = TokenPrimaryGroup; };
        template <> struct MapTokenStructToInfoClass<TOKEN_DEFAULT_DACL> { static const TOKEN_INFORMATION_CLASS infoClass = TokenDefaultDacl; };
        template <> struct MapTokenStructToInfoClass<TOKEN_SOURCE> { static const TOKEN_INFORMATION_CLASS infoClass = TokenSource; };
        template <> struct MapTokenStructToInfoClass<TOKEN_TYPE> { static const TOKEN_INFORMATION_CLASS infoClass = TokenType; };
        template <> struct MapTokenStructToInfoClass<SECURITY_IMPERSONATION_LEVEL> { static const TOKEN_INFORMATION_CLASS infoClass = TokenImpersonationLevel; };
        template <> struct MapTokenStructToInfoClass<TOKEN_STATISTICS> { static const TOKEN_INFORMATION_CLASS infoClass = TokenStatistics; };
        template <> struct MapTokenStructToInfoClass<TOKEN_GROUPS_AND_PRIVILEGES> { static const TOKEN_INFORMATION_CLASS infoClass = TokenGroupsAndPrivileges; };
        template <> struct MapTokenStructToInfoClass<TOKEN_ORIGIN> { static const TOKEN_INFORMATION_CLASS infoClass = TokenOrigin; };
        template <> struct MapTokenStructToInfoClass<TOKEN_ELEVATION_TYPE> { static const TOKEN_INFORMATION_CLASS infoClass = TokenElevationType; };
        template <> struct MapTokenStructToInfoClass<TOKEN_ELEVATION> { static const TOKEN_INFORMATION_CLASS infoClass = TokenElevation; };
        template <> struct MapTokenStructToInfoClass<TOKEN_ACCESS_INFORMATION> { static const TOKEN_INFORMATION_CLASS infoClass = TokenAccessInformation; };
        template <> struct MapTokenStructToInfoClass<TOKEN_MANDATORY_LABEL> { static const TOKEN_INFORMATION_CLASS infoClass = TokenIntegrityLevel; };
        template <> struct MapTokenStructToInfoClass<TOKEN_MANDATORY_POLICY> { static const TOKEN_INFORMATION_CLASS infoClass = TokenMandatoryPolicy; };
        template <> struct MapTokenStructToInfoClass<TOKEN_APPCONTAINER_INFORMATION> { static const TOKEN_INFORMATION_CLASS infoClass = TokenAppContainerSid; };
    }
    /// @endcond

    enum class OpenThreadTokenAs
    {
        Current,
        Self
    };

    /** Open the active token.
    Opens either the current thread token (if impersonating) or the current process token. Returns a token the caller
    can use with methods like GetTokenInformation<> below. By default, the token is opened for TOKEN_QUERY and as the
    effective user.

    Consider using GetCurrentThreadEffectiveToken() instead of this method when eventually calling GetTokenInformation.
    This method returns a real handle to the effective token, but GetCurrentThreadEffectiveToken() is a Pseudo-handle
    and much easier to manage.
    ~~~~
    wil::unique_handle theToken;
    RETURN_IF_FAILED(wil::OpenCurrentAccessTokenNoThrow(&theToken));
    ~~~~
    Callers who want more access to the token (such as to duplicate or modify the token) can pass
    any mask of the token rights.
    ~~~~
    wil::unique_handle theToken;
    RETURN_IF_FAILED(wil::OpenCurrentAccessTokenNoThrow(&theToken, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES));
    ~~~~
    Services impersonating their clients may need to request that the active token is opened on the
    behalf of the service process to perform certain operations. Opening a token for impersonation access
    or privilege-adjustment are examples of uses.
    ~~~~
    wil::unique_handle callerToken;
    RETURN_IF_FAILED(wil::OpenCurrentAccessTokenNoThrow(&theToken, TOKEN_QUERY | TOKEN_IMPERSONATE, true));
    ~~~~
    @param tokenHandle Receives the token opened during the operation. Must be CloseHandle'd by the caller, or
                (preferably) stored in a wil::unique_handle
    @param access Bits from the TOKEN_* access mask which are passed to OpenThreadToken/OpenProcessToken
    @param asSelf When true, and if the thread is impersonating, the thread token is opened using the
                process token's rights.
    */
    inline HRESULT OpenCurrentAccessTokenNoThrow(_Out_ HANDLE* tokenHandle, _In_ unsigned long access = TOKEN_QUERY, _In_ OpenThreadTokenAs openAs = OpenThreadTokenAs::Current)
    {
        HRESULT hr = (OpenThreadToken(GetCurrentThread(), access, (openAs == OpenThreadTokenAs::Self), tokenHandle) ? S_OK : HRESULT_FROM_WIN32(::GetLastError()));
        if (hr == HRESULT_FROM_WIN32(ERROR_NO_TOKEN))
        {
            hr = (OpenProcessToken(GetCurrentProcess(), access, tokenHandle) ? S_OK : HRESULT_FROM_WIN32(::GetLastError()));
        }
        return hr;
    }

    //! Current thread or process token, consider using GetCurrentThreadEffectiveToken() instead.
    inline wil::unique_handle OpenCurrentAccessTokenFailFast(_In_ unsigned long access = TOKEN_QUERY, _In_ OpenThreadTokenAs openAs = OpenThreadTokenAs::Current)
    {
        HANDLE rawTokenHandle;
        FAIL_FAST_IF_FAILED(OpenCurrentAccessTokenNoThrow(&rawTokenHandle, access, openAs));
        return wil::unique_handle(rawTokenHandle);
    }

// Exception based function to open current thread/process access token and acquire pointer to it
#ifdef WIL_ENABLE_EXCEPTIONS
    //! Current thread or process token, consider using GetCurrentThreadEffectiveToken() instead.
    inline wil::unique_handle OpenCurrentAccessToken(_In_ unsigned long access = TOKEN_QUERY, _In_ OpenThreadTokenAs openAs = OpenThreadTokenAs::Current)
    {
        HANDLE rawTokenHandle;
        THROW_IF_FAILED(OpenCurrentAccessTokenNoThrow(&rawTokenHandle, access, openAs));
        return wil::unique_handle(rawTokenHandle);
    }
#endif // WIL_ENABLE_EXCEPTIONS

    /** Fetches information about a token.
    See GetTokenInformation on MSDN for what this method can return. Information is returned to the caller as a
    wistd::unique_ptr<T> where T is one of the TOKEN_* structures (like TOKEN_ORIGIN, TOKEN_USER, TOKEN_ELEVATION, etc.)
    The caller must have access to read the information from the provided token. This method works with both real token
    handles (like those OpenCurrentAccessToken) as well as meta-tokens like GetCurrentThreadEffectiveToken() and
    GetCurrentThreadToken().
    ~~~~
    // Retrieve the TOKEN_USER structure for the current process
    wistd::unique_ptr<TOKEN_USER> user;
    RETURN_IF_FAILED(wil::GetTokenInformationNoThrow(user, GetCurrentProcessToken()));
    RETURN_IF_FAILED(ConsumeSid(user->User.Sid));
    ~~~~
    Pass 'nullptr' as tokenHandle to retrieve information about the effective token.
    ~~~~
    wistd::unique_ptr<TOKEN_PRIVILEGES> privileges;
    RETURN_IF_FAILED(wil::GetTokenInformationNoThrow(privileges, nullptr));
    for (DWORD i = 0; i < privileges->PrivilegeCount; ++i)
    {
        RETURN_IF_FAILED(ConsumePrivilege(privileges->Privileges[i]));
    }
    ~~~~
    @param tokenInfo Receives a pointer to a structure containing the results of GetTokenInformation for the requested
            type. The type of <T> selects which TOKEN_INFORMATION_CLASS will be used.
    @param tokenHandle Specifies which token will be queried. When nullptr, the thread's effective current token is used.
    @return S_OK on success, a FAILED hresult containing the win32 error from querying the token otherwise.
    */
    template <typename T>
    inline HRESULT GetTokenInformationNoThrow(_Inout_ wistd::unique_ptr<T>& tokenInfo, _In_opt_ HANDLE tokenHandle)
    {
        tokenInfo.reset();

        // get the tokenHandle from current thread/process if it's null
        if (tokenHandle == nullptr)
        {
            tokenHandle = GetCurrentThreadEffectiveToken(); // Pseudo token, don't free.
        }

        const auto infoClass = details::MapTokenStructToInfoClass<T>::infoClass;
        DWORD tokenInfoSize = 0;
        RETURN_LAST_ERROR_IF(!((!GetTokenInformation(tokenHandle, infoClass, nullptr, 0, &tokenInfoSize)) &&
            (::GetLastError() == ERROR_INSUFFICIENT_BUFFER)));

        wistd::unique_ptr<char> tokenInfoClose(
            static_cast<char*>(operator new(tokenInfoSize, std::nothrow)));
        RETURN_IF_NULL_ALLOC(tokenInfoClose.get());
        RETURN_IF_WIN32_BOOL_FALSE(GetTokenInformation(tokenHandle, infoClass, tokenInfoClose.get(), tokenInfoSize, &tokenInfoSize));
        tokenInfo.reset(reinterpret_cast<T *>(tokenInfoClose.release()));

        return S_OK;
    }

    template<> inline HRESULT GetTokenInformationNoThrow(_Inout_ wistd::unique_ptr<TOKEN_LINKED_TOKEN>&, _In_opt_ HANDLE) = delete;

    //! Overload of GetTokenInformationNoThrow that retrieves a token linked from the provided token
    inline HRESULT GetTokenInformationNoThrow(_Inout_ unique_token_linked_token& tokenInfo, _In_opt_ HANDLE tokenHandle)
    {
        tokenInfo.reset();
        static_assert(sizeof(tokenInfo) == sizeof(TOKEN_LINKED_TOKEN), "confusing size mismatch");

        if (!tokenHandle)
        {
            tokenHandle = GetCurrentThreadEffectiveToken();
        }

        DWORD tokenInfoSize = 0;
        RETURN_IF_WIN32_BOOL_FALSE(::GetTokenInformation(tokenHandle, TokenLinkedToken, &tokenInfo, sizeof(tokenInfo), &tokenInfoSize));

        return S_OK;
    }

    //! A variant of GetTokenInformation<T> that fails-fast on errors retrieving the token
    template <typename T>
    inline wistd::unique_ptr<T> GetTokenInformationFailFast(_In_ HANDLE token = nullptr)
    {
        wistd::unique_ptr<T> tokenInfo;
        FAIL_FAST_IF_FAILED(GetTokenInformationNoThrow(tokenInfo, token));
        return tokenInfo;
    }

    /** Retrieves the linked-token information for a token.
    Fails-fast if the link information cannot be retrieved.
    ~~~~
    auto link = GetLinkedTokenInformationFailFast(GetCurrentThreadToken());
    auto tokenUser = GetTokenInformation<TOKEN_USER>(link.LinkedToken);
    ~~~~
    @param token Specifies the token to query. Pass nullptr to use the current effective thread token
    @return unique_token_linked_token containing a handle to the linked token
    */
    inline unique_token_linked_token GetLinkedTokenInformationFailFast(_In_ HANDLE token = nullptr)
    {
        unique_token_linked_token tokenInfo;
        FAIL_FAST_IF_FAILED(GetTokenInformationNoThrow(tokenInfo, token));
        return tokenInfo;
    }

#ifdef WIL_ENABLE_EXCEPTIONS
    /** Fetches information about a token.
    See GetTokenInformationNoThrow for full details.
    ~~~~
    auto user = wil::GetTokenInformation<TOKEN_USER>(GetCurrentProcessToken());
    ConsumeSid(user->User.Sid);
    ~~~~
    Pass 'nullptr' (or omit the parameter) as tokenHandle to retrieve information about the effective token.
    ~~~~
    auto privs = wil::GetTokenInformation<TOKEN_PRIVILEGES>(privileges);
    for (auto& priv : wil::make_range(privs->Privileges, privs->Privilieges + privs->PrivilegeCount))
    {
        if (priv.Attributes & SE_PRIVILEGE_ENABLED)
        {
            // ...
        }
    }
    ~~~~
    @return A pointer to a structure containing the results of GetTokenInformation for the requested  type. The type of
                <T> selects which TOKEN_INFORMATION_CLASS will be used.
    @param token Specifies which token will be queried. When nullptr or not set, the thread's effective current token is used.
    */
    template <typename T>
    inline wistd::unique_ptr<T> GetTokenInformation(_In_ HANDLE token = nullptr)
    {
        wistd::unique_ptr<T> tokenInfo;
        THROW_IF_FAILED(GetTokenInformationNoThrow(tokenInfo, token));
        return tokenInfo;
    }

    /** Retrieves the linked-token information for a token.
    Throws an exception if the link information cannot be retrieved.
    ~~~~
    auto link = GetLinkedTokenInformation(GetCurrentThreadToken());
    auto tokenUser = GetTokenInformation<TOKEN_USER>(link.LinkedToken);
    ~~~~
    @param token Specifies the token to query. Pass nullptr to use the current effective thread token
    @return unique_token_linked_token containing a handle to the linked token
    */
    inline unique_token_linked_token GetLinkedTokenInformation(_In_ HANDLE token = nullptr)
    {
        unique_token_linked_token tokenInfo;
        THROW_IF_FAILED(GetTokenInformationNoThrow(tokenInfo, token));
        return tokenInfo;
    }
#endif

    // http://osgvsowi/658484 To track DSMA(Default System Managed Account) workitem, below implementation will be updated once above workitem is implemented
    // To check if DSMA or not
    inline bool IsDefaultSystemManagedAccount(_In_opt_ PCWSTR userName = nullptr)
    {
        // This is temporary DefaultAccount Scenario Code
        // User OOBE reg keys/values
        const PCWSTR regValueDefaultAccountName = L"DefaultAccountSAMName";
        const PCWSTR regKeyOOBE = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OOBE";
        wchar_t defaultAccountName[UNLEN + 1];
        DWORD defaultAccountNameByteSize = sizeof(defaultAccountName);
        wchar_t currentUserName[DNLEN + UNLEN + 2];     // +2 for "\" delimeter and terminating NULL

        // Get SAM name for current user if no user name specified.
        if (userName == nullptr)
        {
            DWORD currentUserNameSize = ARRAYSIZE(currentUserName);
            if (GetUserNameExW(NameSamCompatible, currentUserName, &currentUserNameSize))
            {
                userName = wcschr(currentUserName, L'\\') + 1;
            }
        }

        return SUCCEEDED(HRESULT_FROM_WIN32(RegGetValueW(HKEY_LOCAL_MACHINE, regKeyOOBE, regValueDefaultAccountName, RRF_RT_REG_SZ, nullptr, defaultAccountName, &defaultAccountNameByteSize))) &&
            (userName != nullptr) &&
            (CompareStringOrdinal(userName, -1, defaultAccountName, -1, TRUE) == CSTR_EQUAL);
    }

    /// @cond
    namespace details
    {
        inline void RevertImpersonateToken(_Pre_opt_valid_ _Frees_ptr_opt_ HANDLE oldToken)
        {
            FAIL_FAST_IMMEDIATE_IF(!::SetThreadToken(nullptr, oldToken));

            if (oldToken)
            {
                ::CloseHandle(oldToken);
            }
        }
    }
    /// @endcond

    using unique_token_reverter = wil::unique_any<
        HANDLE,
        decltype(&details::RevertImpersonateToken),
        details::RevertImpersonateToken,
        details::pointer_access_none,
        HANDLE,
        INT_PTR,
        -1,
        HANDLE>;

    /** Temporarily impersonates a token on this thread.
    This method sets a new token on a thread, restoring the current token when the returned object
    is destroyed. Useful for impersonating other tokens or running as 'self,' especially in services.
    ~~~~
    HRESULT OpenFileAsSessionuser(_In_z_ const wchar_t* filePath, DWORD session, _Out_ HANDLE* opened)
    {
        wil::unique_handle userToken;
        RETURN_IF_WIN32_BOOL_FALSE(QueryUserToken(session, &userToken));

        wil::unique_token_reverter reverter;
        RETURN_IF_FAILED(wil::impersonate_token_nothrow(userToken.get(), reverter));

        wil::unique_hfile userFile(::CreateFile(filePath, ...));
        RETURN_LAST_ERROR_IF(!userFile && (::GetLastError() != ERROR_FILE_NOT_FOUND));

        *opened = userFile.release();
        return S_OK;
    }
    ~~~~
    @param token A token to impersonate, or 'nullptr' to run as the process identity.
    */
    inline HRESULT impersonate_token_nothrow(_In_ HANDLE token, _Out_ unique_token_reverter& reverter)
    {
        wil::unique_handle currentToken;

        // Get the token for the current thread. If there wasn't one, the reset will clear it as well
        if (!OpenThreadToken(GetCurrentThread(), TOKEN_ALL_ACCESS, TRUE, &currentToken))
        {
            RETURN_LAST_ERROR_IF(::GetLastError() != ERROR_NO_TOKEN);
        }

        // Update the current token
        RETURN_IF_WIN32_BOOL_FALSE(::SetThreadToken(nullptr, token));

        reverter.reset(currentToken.release()); // Ownership passed
        return S_OK;
    }

    /** Temporarily clears any impersonation on this thread.
    This method resets the current thread's token to nullptr, indicating that it is not impersonating
    any user. Useful for elevating to whatever identity a service or higher-privilege process might
    be capable of running under.
    ~~~~
    HRESULT DeleteFileRetryAsSelf(_In_z_ const wchar_t* filePath)
    {
        if (!::DeleteFile(filePath))
        {
            RETURN_LAST_ERROR_IF(::GetLastError() != ERROR_ACCESS_DENIED);
            wil::unique_token_reverter reverter;
            RETURN_IF_FAILED(wil::run_as_self_nothrow(reverter));
            RETURN_IF_FAILED(TakeOwnershipOfFile(filePath));
            RETURN_IF_FAILED(GrantDeleteAccess(filePath));
            RETURN_IF_WIN32_BOOL_FALSE(::DeleteFile(filePath));
        }
        return S_OK;
    }
    ~~~~
    */
    inline HRESULT run_as_self_nothrow(_Out_ unique_token_reverter& reverter)
    {
        return impersonate_token_nothrow(nullptr, reverter);
    }

    inline unique_token_reverter impersonate_token_failfast(_In_ HANDLE token)
    {
        unique_token_reverter oldToken;
        FAIL_FAST_IF_FAILED(impersonate_token_nothrow(token, oldToken));
        return oldToken;
    }

    inline unique_token_reverter run_as_self_failfast()
    {
        return impersonate_token_failfast(nullptr);
    }

#ifdef WIL_ENABLE_EXCEPTIONS
    /** Temporarily impersonates a token on this thread.
    This method sets a new token on a thread, restoring the current token when the returned object
    is destroyed. Useful for impersonating other tokens or running as 'self,' especially in services.
    ~~~~
    wil::unique_hfile OpenFileAsSessionuser(_In_z_ const wchar_t* filePath, DWORD session)
    {
        wil::unique_handle userToken;
        THROW_IF_WIN32_BOOL_FALSE(QueryUserToken(session, &userToken));

        auto priorToken = wil::impersonate_token(userToken.get());

        wil::unique_hfile userFile(::CreateFile(filePath, ...));
        THROW_LAST_ERROR_IF(::GetLastError() != ERROR_FILE_NOT_FOUND);

        return userFile;
    }
    ~~~~
    @param token A token to impersonate, or 'nullptr' to run as the process identity.
    */
    inline unique_token_reverter impersonate_token(_In_opt_ HANDLE token)
    {
        unique_token_reverter oldToken;
        THROW_IF_FAILED(impersonate_token_nothrow(token, oldToken));
        return oldToken;
    }

    /** Temporarily clears any impersonation on this thread.
    This method resets the current thread's token to nullptr, indicating that it is not impersonating
    any user. Useful for elevating to whatever identity a service or higher-privilege process might
    be capable of running under.
    ~~~~
    void DeleteFileRetryAsSelf(_In_z_ const wchar_t* filePath)
    {
        if (!::DeleteFile(filePath) && (::GetLastError() == ERROR_ACCESS_DENIED))
        {
            auto priorToken = wil::run_as_self();
            TakeOwnershipOfFile(filePath);
            GrantDeleteAccess(filePath);
            ::DeleteFile(filePath);
        }
    }
    ~~~~
    */
    inline unique_token_reverter run_as_self()
    {
        return impersonate_token(nullptr);
    }
#endif // WIL_ENABLE_EXCEPTIONS

    /** Determines whether a specified security identifier (SID) is enabled in an access token.
    This function determines whether a security identifier, described by a given set of subauthorities, is enabled
    in the given access token. Note that only up to eight subauthorities can be passed to this function.
    ~~~~
    bool IsGuest()
    {
        return wil::test_token_membership(nullptr, SECURITY_NT_AUTHORITY, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_GUESTS));
    }
    ~~~~
    @param result This will be set to true if and only if a security identifier described by the given set of subauthorities is enabled in the given access token.
    @param token A handle to an access token. The handle must have TOKEN_QUERY access to the token, and must be an impersonation token. If token is nullptr, test_token_membership
           uses the impersonation token of the calling thread. If the thread is not impersonating, the function duplicates the thread's primary token to create an impersonation token.
    @param sidAuthority A reference to a SID_IDENTIFIER_AUTHORITY structure. This structure provides the top-level identifier authority value to set in the SID.
    @param subAuthorities Up to eight subauthority values to place in the SID (this is a limit of AllocateAndInitializeSid).
    @return S_OK on success, a FAILED hresult containing the win32 error from creating the SID or querying the token otherwise.
    */
    template<typename... Ts> HRESULT test_token_membership_nothrow(_Out_ bool* result, _In_opt_ HANDLE token, _In_ const SID_IDENTIFIER_AUTHORITY& sidAuthority, _In_ Ts... subAuthorities)
    {
        *result = false;

        static_assert(sizeof...(subAuthorities) <= 8, "The maximum allowed number of subauthorities is 8 (limitation of AllocateAndInitializeSid)");

        DWORD subAuthorityArray[8] = { subAuthorities... };
        unique_any<PSID, decltype(&::FreeSid), ::FreeSid> groupSid;
        RETURN_IF_WIN32_BOOL_FALSE(AllocateAndInitializeSid(const_cast<PSID_IDENTIFIER_AUTHORITY>(&sidAuthority), static_cast<BYTE>(sizeof...(subAuthorities)), subAuthorityArray[0], subAuthorityArray[1], subAuthorityArray[2], subAuthorityArray[3], subAuthorityArray[4], subAuthorityArray[5], subAuthorityArray[6], subAuthorityArray[7], &groupSid));

        BOOL isMember;
        RETURN_IF_WIN32_BOOL_FALSE(CheckTokenMembership(token, groupSid.get(), &isMember));

        *result = (isMember != FALSE);

        return S_OK;
    }

    /** Determine whether a token represents an app container
    This method uses the passed in token and emits a boolean indicating that
    whether TokenIsAppContainer is true.
    ~~~~
    HRESULT OnlyIfAppContainer()
    {
    bool isAppContainer;
    RETURN_IF_FAILED(wil::GetTokenIsAppContainerNoThrow(nullptr, isAppContainer));
    RETURN_HR_IF(E_ACCESSDENIED, !isAppContainer);
    RETURN_HR(...);
    }
    ~~~~
    @param token A token to get info about, or 'nullptr' to run as the current thread.
    */
    inline HRESULT GetTokenIsAppContainerNoThrow(_In_opt_ HANDLE token, _Out_ bool& value)
    {
        DWORD isAppContainer = 0;
        DWORD returnLength = 0;
        RETURN_IF_WIN32_BOOL_FALSE(::GetTokenInformation(
            token ? token : GetCurrentThreadEffectiveToken(),
            TokenIsAppContainer,
            &isAppContainer,
            sizeof(isAppContainer),
            &returnLength));

        value = (isAppContainer != 0);

        return S_OK;
    }

    //! A variant of GetTokenIsAppContainerNoThrow that fails-fast on errors retrieving the token information
    inline bool GetTokenIsAppContainerFailFast(_In_opt_ HANDLE token = nullptr)
    {
        bool value = false;
        FAIL_FAST_IF_FAILED(GetTokenIsAppContainerNoThrow(token, value));

        return value;
    }

#ifdef WIL_ENABLE_EXCEPTIONS
    //! A variant of GetTokenIsAppContainerNoThrow that throws on errors retrieving the token information
    inline bool GetTokenIsAppContainer(_In_opt_ HANDLE token = nullptr)
    {
        bool value = false;
        THROW_IF_FAILED(GetTokenIsAppContainerNoThrow(token, value));

        return value;
    }
#endif // WIL_ENABLE_EXCEPTIONS

    template<typename... Ts> bool test_token_membership_failfast(_In_opt_ HANDLE token, _In_ const SID_IDENTIFIER_AUTHORITY& sidAuthority, _In_ Ts&&... subAuthorities)
    {
        bool result;
        FAIL_FAST_IF_FAILED(test_token_membership_nothrow(&result, token, sidAuthority, static_cast<Ts&&>(subAuthorities)...));
        return result;
    }

#ifdef WIL_ENABLE_EXCEPTIONS
    template<typename... Ts> bool test_token_membership(_In_opt_ HANDLE token, _In_ const SID_IDENTIFIER_AUTHORITY& sidAuthority, _In_ Ts&&... subAuthorities)
    {
        bool result;
        THROW_IF_FAILED(test_token_membership_nothrow(&result, token, sidAuthority, static_cast<Ts&&>(subAuthorities)...));
        return result;
    }
#endif

} //namespace wil

#endif // __WIL_TOKEN_HELPERS_INCLUDED