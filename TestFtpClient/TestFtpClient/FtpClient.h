#ifndef _FtpClient_H_
#define _FtpClient_H_

#include <string>
#include <vector>
#include <deque>
#include <curl/curl.h>
#include <Poco/Mutex.h>
#include <Poco/Thread.h>
#include <Poco/Runnable.h>

#include "AtomicBool.h"

class ProgressObserver
{
public:
    virtual ~ProgressObserver(){}

    virtual void OnUploadProgress(
        const std::string &sFileName,
        long iCurSize,
        long iTotalSize) = 0;

    virtual void OnDownloadProgress(
        const std::string &sFileName,
        long iCurSize,
        long iTotalSize) = 0;
};

class FtpClient;
struct FtpParam
{
    FILE *pFileHandle;
    std::string sFileName;
    long iCurSize;
    long iTotalSize;
    FtpClient *pClient;
    void (FtpClient::*pFunc)(const void*);
    volatile bool bCancel;
    Poco::FastMutex theMutex;
};

class FtpClient
: public Poco::Runnable
, private Poco::Thread
{
    enum OptMode
    {
        Unknown,
        Upload,
        Download,
    };

public:
    FtpClient(const std::string &sUserPwd = "admin:123456");

    ~FtpClient();

    void SetUserPwd(const std::string &sUserPwd);

    const std::string GetUserPwd() const;

    bool RegisterObserver(ProgressObserver *observer);

    bool UnRegisterObserver(ProgressObserver *observer);

    bool GetCurProcess(
        std::string &sFileName,
        long &iCurSize,
        long &iTotalSize);

    bool UploadFileSync(
        const std::string &sRemotePath,
        const std::string &sLocalPath,
        const std::string &sUserPwd = "");

    bool UploadFileAsync(
        const std::string &sRemotePath,
        const std::string &sLocalPath,
        const std::string &sUserPwd = "");

    bool UploadDirAllFilesAsync(
        const std::string &sRemoteDirectory,
        const std::string &sLocalDirectory,
        const std::string &sUserPwd = "");

    bool UploadDirMatchedFilesAsync(
        const std::string &sRemoteDirectory,
        const std::string &sLocalDirectory,
        const std::vector<std::string> &vectMatch,
        bool bMatch = true,
        const std::string &sUserPwd = "");

    bool DownloadFileSync(
        const std::string &sRemotePath,
        const std::string &sLocalPath,
        const std::string &sUserPwd = "");

    bool DownloadFileAsync(
        const std::string &sRemotePath,
        const std::string &sLocalPath,
        const std::string &sUserPwd = "");

    bool AwaitResult();

    bool Cancel();

protected:
    void run();

private:
    bool SetStartState(const std::string &sUserPwd,
        OptMode eCurOptMode);

    bool UploadFileImpl(
        const std::string &sRemotePath,
        const std::string &sLocalPath,
        int iTimeout);

    bool DownloadFileImpl(
        const std::string &sRemotePath,
        const std::string &sLocalPath,
        int iTimeout);

    void OnUpload(const void *pParam);

    void OnDownLoad(const void *pParam);

    FtpClient(const FtpClient &rhs);

    FtpClient & operator=(const FtpClient &rhs);

private:
    std::vector<ProgressObserver *> m_Observers;
    std::deque<std::pair<std::string, std::string> > m_UploadTasks;

    Poco::FastMutex m_CallbackMutex;

    std::string m_sLocalPath;
    std::string m_sRemotePath;
    std::string m_sUserPwd;
    OptMode m_eCurOptMode;
    bool m_bOptResult;

    FtpParam m_FtpParam;
    AtomicBool m_bRoutineStart;
};

#endif // _FtpClient_H_