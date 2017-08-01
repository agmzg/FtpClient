#include <Poco/Path.h>
#include <Poco/File.h>
#include <Poco/DirectoryIterator.h>

#include "FtpClient.h"

namespace // anonymous namespace begin
{
    /* read data to upload */
    size_t _ReadData(
        void *pData,
        size_t size,
        size_t nmemb,
        void *pParam)
    {
        FtpParam *pFtpParam = (FtpParam *)pParam;
        FILE *pFileHandle = pFtpParam->pFileHandle;

        if (ferror(pFileHandle))
        {
            return CURL_READFUNC_ABORT;
        }

        size_t iRead = fread(pData, size, nmemb, pFileHandle) * size;
        {
            Poco::FastMutex::ScopedLock l(pFtpParam->theMutex);
            pFtpParam->iCurSize += iRead;
        }

        (pFtpParam->pClient->*(pFtpParam->pFunc))(pParam);

        return iRead;
    }

    /* write data to download */
    size_t _WriteData(
        void *pData,
        size_t size,
        size_t nmemb,
        void *pParam)
    {
        FtpParam *pFtpParam = (FtpParam *)pParam;
        FILE *pFileHandle = pFtpParam->pFileHandle;

        if (ferror(pFileHandle))
        {
            return CURL_READFUNC_ABORT;
        }

        size_t iWrite = fwrite(pData, size, nmemb, pFileHandle) * size;
        {
            Poco::FastMutex::ScopedLock l(pFtpParam->theMutex);
            pFtpParam->iCurSize = ftell(pFileHandle);
        }

        (pFtpParam->pClient->*(pFtpParam->pFunc))(pParam);

        return iWrite;
    }

    size_t _ThrowAway(
        void *pData,
        size_t size,
        size_t nmemb,
        void *pParam)
    {
        (void)pData;
        (void)pParam;
        return (size_t)(size * nmemb);
    }

    bool _GetUrlFileSize(
        double &fileSize,
        const std::string &sUrl,
        const std::string &sUserPwd)
    {
        CURL *pCurl = curl_easy_init();
        if (pCurl)
        {
            curl_easy_setopt(pCurl, CURLOPT_URL, sUrl.c_str());
            curl_easy_setopt(pCurl, CURLOPT_USERPWD, sUserPwd.c_str());
            curl_easy_setopt(pCurl, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(pCurl, CURLOPT_HEADERFUNCTION, _ThrowAway);
            curl_easy_setopt(pCurl, CURLOPT_HEADER, 0L);

            CURLcode res = curl_easy_perform(pCurl);
            if (CURLE_OK == res)
            {
                res = curl_easy_getinfo(pCurl,
                    CURLINFO_CONTENT_LENGTH_DOWNLOAD, &fileSize);

                curl_easy_cleanup(pCurl);
                return (CURLE_OK == res) && (fileSize > 0.0);
            }
            else
            {
                fprintf(stderr, "%s\n", curl_easy_strerror(res));
            }
        }
        curl_easy_cleanup(pCurl);
        return false;
    }

    int _Progress(
        void *pParam,
        double dltotal,
        double dlnow,
        double ultotal,
        double ulnow)
    {
        (void)dltotal;
        (void)dlnow;
        (void)ultotal;
        (void)ulnow;
        FtpParam *pFtpParam = (FtpParam *)pParam;
        if (pFtpParam && pFtpParam->bCancel)
        {
            return -1;
        }
        return 0;
    }

    bool _GetUploadTasks(
        const std::string &sLocalDirectory,
        const std::string &sRemoteDirectory,
        std::deque<std::pair<std::string, std::string> > & uploadTasks,
        long &iTotalSize)
    {
        iTotalSize = 0;
        std::string sUrlDirectory = sRemoteDirectory;
        if (sRemoteDirectory.back() != '/')
        {
            sUrlDirectory += "/";
        }
        Poco::DirectoryIterator it(sLocalDirectory), end;
        while (it != end)
        {
            if (it->isFile())
            {
                std::string sRemotePath = sUrlDirectory +
                    Poco::Path(it->path()).getFileName();
                uploadTasks.push_back(std::make_pair(sRemotePath,
                    it->path()));
                iTotalSize += (long)it->getSize();
            }
            ++it;
        }

        return !uploadTasks.empty();
    }

    void _GetProcessInfoWithLock(
        FtpParam *pFtpParam,
        std::string &sFileName,
        long &iCurSize,
        long &iTotalSize)
    {
        {
            Poco::FastMutex::ScopedLock l(pFtpParam->theMutex);
            sFileName = pFtpParam->sFileName;
            iCurSize = pFtpParam->iCurSize;
            iTotalSize = pFtpParam->iTotalSize;
        }
        return;
    }
} // anonymous namespace end

FtpClient::FtpClient(const std::string &sUserPwd/* = "admin:123456"*/)
: Poco::Runnable()
, Poco::Thread()
, m_CallbackMutex()
, m_sLocalPath()
, m_sRemotePath()
, m_sUserPwd(sUserPwd)
, m_eCurOptMode(Unknown)
, m_bOptResult(false)
, m_bRoutineStart(false)
{
}

FtpClient::~FtpClient()
{
    Cancel();
    AwaitResult();
}

void FtpClient::SetUserPwd(const std::string &sUserPwd)
{
    m_sUserPwd = sUserPwd;
}

const std::string FtpClient::GetUserPwd() const
{
    return m_sUserPwd;
}

bool FtpClient::RegisterObserver(ProgressObserver *observer)
{
    Poco::FastMutex::ScopedLock lock(m_CallbackMutex);
    if (std::find(m_Observers.begin(), m_Observers.end(), observer)
        == m_Observers.end())
    {
        m_Observers.push_back(observer);
        return true;
    }
    return false;
}

bool FtpClient::UnRegisterObserver(ProgressObserver *observer)
{
    Poco::FastMutex::ScopedLock lock(m_CallbackMutex);
    std::vector<ProgressObserver *>::iterator it =
        std::find(m_Observers.begin(), m_Observers.end(), observer);
    if (it != m_Observers.end())
    {
        m_Observers.erase(it);
        return true;
    }
    return false;
}

bool FtpClient::UploadFileSync(
    const std::string &sRemotePath,
    const std::string &sLocalPath,
    const std::string &sUserPwd/* = ""*/)
{
    if (UploadFileAsync(sRemotePath, sLocalPath, sUserPwd))
    {
        return AwaitResult();
    }
    return false;
}

bool FtpClient::UploadFileAsync(
    const std::string &sRemotePath,
    const std::string &sLocalPath,
    const std::string &sUserPwd/* = ""*/)
{
    if (SetStartState(sUserPwd, Upload))
    {
        if (!Poco::File(sLocalPath).exists() ||
            !Poco::File(sLocalPath).isFile()) return false;

        m_UploadTasks.push_back(std::make_pair(sRemotePath, sLocalPath));
        {
            Poco::FastMutex::ScopedLock l(m_FtpParam.theMutex);
            m_FtpParam.iTotalSize =
                (long)Poco::File(sLocalPath).getSize();
        }
        Poco::Thread::start(*this);
        return true;
    }
    fprintf(stderr, "Routine is Running\n");
    return false;
}

bool FtpClient::UploadDirectoryAsync(
    const std::string &sRemoteDirectory,
    const std::string &sLocalDirectory,
    const std::string &sUserPwd/* = ""*/)
{
    if (!Poco::File(sLocalDirectory).exists() ||
        !Poco::Path(sLocalDirectory).isDirectory()) return false;

    if (SetStartState(sUserPwd, Upload))
    {
        {
            Poco::FastMutex::ScopedLock l(m_FtpParam.theMutex);
            _GetUploadTasks(sLocalDirectory, sRemoteDirectory,
                m_UploadTasks, m_FtpParam.iTotalSize);
        }
        Poco::Thread::start(*this);
        return true;
    }
    fprintf(stderr, "Routine is Running\n");
    return false;
}

bool FtpClient::DownloadFileSync(
    const std::string &sRemotePath,
    const std::string &sLocalPath,
    const std::string &sUserPwd/* = ""*/)
{
    if (DownloadFileAsync(sRemotePath, sLocalPath, sUserPwd))
    {
        return AwaitResult();
    }
    return false;
}

bool FtpClient::DownloadFileAsync(
    const std::string &sRemotePath,
    const std::string &sLocalPath,
    const std::string &sUserPwd/* = ""*/)
{
    if (SetStartState(sUserPwd, Download))
    {
        m_sRemotePath = sRemotePath;
        m_sLocalPath = sLocalPath;
        Poco::Thread::start(*this);
        return true;
    }
    fprintf(stderr, "Routine is Running\n");
    return false;
}

bool FtpClient::AwaitResult()
{
    try
    {
        while (m_bRoutineStart.Load() ||
               Poco::Thread::isRunning())
        {
            Poco::Thread::yield();
        }
    }
    catch (const std::exception &e)
    {
        fprintf(stderr, "%s\n", e.what());
        return false;
    }
    return m_bOptResult;
}

bool FtpClient::Cancel()
{
    if (m_bRoutineStart.Load())
    {
        m_FtpParam.bCancel = true;
        return true;
    }

    return false;
}

bool FtpClient::SetStartState(
    const std::string &sUserPwd,
    OptMode eCurOptMode)
{
    bool bExpcted = false;
    if (m_bRoutineStart.CompareExchange(bExpcted, true))
    {
        if (!sUserPwd.empty())
        {
            m_sUserPwd = sUserPwd;
        }
        m_eCurOptMode = eCurOptMode;

        {
            Poco::FastMutex::ScopedLock l(m_FtpParam.theMutex);
            m_FtpParam.bCancel = false;
            m_FtpParam.iCurSize = 0;
            m_FtpParam.iTotalSize = 0;
        }

        return true;
    }
    return false;
}

void FtpClient::run()
{
    try
    {
        m_bOptResult = false;

        switch (m_eCurOptMode)
        {
        case Upload:
        {
            m_bOptResult = true;
            while (!m_UploadTasks.empty())
            {
                std::pair<std::string, std::string> item =
                    m_UploadTasks.front();
                m_UploadTasks.pop_front();
                if (!UploadFileImpl(item.first, item.second, 3))
                {
                    m_bOptResult = false;
                    break;
                }
            }
            m_UploadTasks.clear();
            break;
        }
        case Download:
            m_bOptResult = DownloadFileImpl(m_sRemotePath, m_sLocalPath, 3);
            break;
        default:
            m_bOptResult = false;
            break;
        }
    }
    catch (...)
    {
        m_bOptResult = false;
    }

    m_bRoutineStart.Store(false);
}

bool FtpClient::UploadFileImpl(
    const std::string &sRemotePath,
    const std::string &sLocalPath,
    int iTimeout)
{
    bool bResult = false;

    FILE *pFileHandle = fopen(sLocalPath.c_str(), "rb");
    if (pFileHandle == NULL)
    {
        perror(NULL);
        return bResult;
    }

    {
        Poco::FastMutex::ScopedLock l(m_FtpParam.theMutex);
        m_FtpParam.sFileName = Poco::Path(sLocalPath).getFileName();
        m_FtpParam.pFileHandle = pFileHandle;
        m_FtpParam.pClient = this;
        m_FtpParam.pFunc = &FtpClient::OnUpload;
    }

    CURL *pCurl = curl_easy_init();
    if (NULL == pCurl)
    {
        fprintf(stderr, "curl_easy_init failed!%d\n", __LINE__);
        return bResult;
    }
    curl_easy_setopt(pCurl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(pCurl, CURLOPT_URL, sRemotePath.c_str());
    curl_easy_setopt(pCurl, CURLOPT_USERPWD, m_sUserPwd.c_str());

    if (iTimeout)
    {
        curl_easy_setopt(pCurl, CURLOPT_FTP_RESPONSE_TIMEOUT, iTimeout);
    }
    // 设置连接超时(秒)
    curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, 5);
    // 使用最低速度超时的方式，避免网络断开导致的线程卡死
    curl_easy_setopt(pCurl, CURLOPT_LOW_SPEED_LIMIT, 1);
    curl_easy_setopt(pCurl, CURLOPT_LOW_SPEED_TIME, 10);

    curl_easy_setopt(pCurl, CURLOPT_READFUNCTION, _ReadData);
    curl_easy_setopt(pCurl, CURLOPT_READDATA, &m_FtpParam);
//     curl_easy_setopt(pCurl, CURLOPT_INFILESIZE_LARGE,
//         (curl_off_t)m_FtpParam.iTotalSize);

    curl_easy_setopt(pCurl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(pCurl, CURLOPT_PROGRESSFUNCTION, _Progress);
    curl_easy_setopt(pCurl, CURLOPT_PROGRESSDATA, &m_FtpParam);

    curl_easy_setopt(pCurl, CURLOPT_FTPPORT, "-"); /* disable passive mode */
    curl_easy_setopt(pCurl, CURLOPT_FTP_CREATE_MISSING_DIRS, 1L);

    //curl_easy_setopt(pCurl, CURLOPT_VERBOSE, 1L);

    CURLcode ret = curl_easy_perform(pCurl);

    fclose(pFileHandle);

    if (ret == CURLE_OK)
    {
        bResult = true;
    }
    else
    {
        fprintf(stderr, "%s\n", curl_easy_strerror(ret));
        bResult = false;
    }
    curl_easy_cleanup(pCurl);
    return bResult;
}

bool FtpClient::DownloadFileImpl(
    const std::string &sRemotePath,
    const std::string &sLocalPath,
    int iTimeout)
{
    bool bResult = false;

    if (!Poco::File(sLocalPath).exists())
    {
        Poco::File(Poco::Path(sLocalPath).parent())
            .createDirectories();
    }
    FILE *pFileHandle = fopen(sLocalPath.c_str(), "wb");
    if (pFileHandle == NULL)
    {
        perror(NULL);
        return false;
    }
    double fileTotalSize;
    if (!_GetUrlFileSize(fileTotalSize, sRemotePath, m_sUserPwd))
    {
        fprintf(stderr, "_GetUrlFileSize failed%d\n", __LINE__);
        return false;
    }

    {
        Poco::FastMutex::ScopedLock l(m_FtpParam.theMutex);
        m_FtpParam.sFileName = Poco::Path(sLocalPath).getFileName();
        m_FtpParam.iTotalSize = (long)fileTotalSize;
        m_FtpParam.pFileHandle = pFileHandle;
        m_FtpParam.pClient = this;
        m_FtpParam.pFunc = &FtpClient::OnDownLoad;
    }

    CURL *pCurl = curl_easy_init();
    curl_easy_setopt(pCurl, CURLOPT_URL, sRemotePath.c_str());
    curl_easy_setopt(pCurl, CURLOPT_USERPWD, m_sUserPwd.c_str());
    //连接超时设置
    if (iTimeout > 0)
    {
        curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, iTimeout);
    }
    // 设置连接超时(秒)
    curl_easy_setopt(pCurl, CURLOPT_CONNECTTIMEOUT, 5);
    // 使用最低速度超时的方式，避免网络断开导致的线程卡死
    curl_easy_setopt(pCurl, CURLOPT_LOW_SPEED_LIMIT, 1);
    curl_easy_setopt(pCurl, CURLOPT_LOW_SPEED_TIME, 10);

    curl_easy_setopt(pCurl, CURLOPT_WRITEFUNCTION, _WriteData);
    curl_easy_setopt(pCurl, CURLOPT_WRITEDATA, &m_FtpParam);

    curl_easy_setopt(pCurl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(pCurl, CURLOPT_PROGRESSFUNCTION, _Progress);
    curl_easy_setopt(pCurl, CURLOPT_PROGRESSDATA, &m_FtpParam);

    //curl_easy_setopt(pCurl, CURLOPT_VERBOSE, 1L);

    CURLcode ret = curl_easy_perform(pCurl);
    fclose(pFileHandle);
    if (ret == CURLE_OK)
    {
        bResult = true;
    }
    else
    {
        fprintf(stderr, "%s\n", curl_easy_strerror(ret));
        bResult = false;
    }
    curl_easy_cleanup(pCurl);

    return bResult;
}

void FtpClient::OnUpload(const void *pParam)
{
    {
        Poco::FastMutex::ScopedLock lock(m_CallbackMutex);
        if (NULL == pParam || m_Observers.empty()) return;
    }
    std::string sFileName;
    long iCurSize, iTotalSize;
    _GetProcessInfoWithLock(
        (FtpParam*)pParam, sFileName, iCurSize, iTotalSize);
    Poco::FastMutex::ScopedLock lock(m_CallbackMutex);
    for (size_t i = 0; i < m_Observers.size(); ++i)
    {
        try
        {
            m_Observers[i]->OnUploadProgress(sFileName,
                iCurSize, iTotalSize);
        }
        catch (...)
        {
        }
    }
}

void FtpClient::OnDownLoad(const void *pParam)
{
    {
        Poco::FastMutex::ScopedLock lock(m_CallbackMutex);
        if (NULL == pParam || m_Observers.empty()) return;
    }
    std::string sFileName;
    long iCurSize, iTotalSize;
    _GetProcessInfoWithLock(
        (FtpParam*)pParam, sFileName, iCurSize, iTotalSize);
    Poco::FastMutex::ScopedLock lock(m_CallbackMutex);
    for (size_t i = 0; i < m_Observers.size(); ++i)
    {
        try
        {
            m_Observers[i]->OnDownloadProgress(sFileName,
                iCurSize, iTotalSize);
        }
        catch (...)
        {
        }
    }
}

bool FtpClient::GetCurProcess(
    std::string &sFileName,
    long &iCurSize,
    long &iTotalSize)
{
    if (m_bRoutineStart.Load())
    {
        Poco::FastMutex::ScopedLock l(m_FtpParam.theMutex);
        sFileName = m_FtpParam.sFileName;
        iCurSize = m_FtpParam.iCurSize;
        iTotalSize = m_FtpParam.iTotalSize;
    }
    return false;
}
