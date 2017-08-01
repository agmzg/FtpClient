#include "FtpClient.h"

class ProgressMonitor : public ProgressObserver
{
public:
    void OnUploadProgress(
        const std::string &sName,
        long iCurSize,
        long iTotalSize)
    {
        if (iTotalSize == 0) return;
        long long iProgress = (long long)iCurSize * 100;
        iProgress /= iTotalSize;
        printf("FileName: %s, upload progress: %d%\n",
            sName.c_str(), iProgress);
    }

    void OnDownloadProgress(
        const std::string &sName,
        long iCurSize,
        long iTotalSize)
    {
        if (iTotalSize == 0) return;
        long long iProgress = (long long)iCurSize * 100;
        iProgress /= iTotalSize;
        printf("FileName: %s, download progress: %d%\n",
            sName.c_str(), iProgress);
    }
};

void TestSync()
{
    ProgressMonitor monitor;
    FtpClient client;
    client.RegisterObserver(&monitor);
    bool bResult = client.UploadFileSync("ftp://192.168.1.170/test/upload.h264",
        "D:\\testFTP\\upload.h264");

    if (bResult)
    {
        printf("UploadFileSync success!\n");
    }

    bResult = client.DownloadFileSync("ftp://192.168.1.170/test/upload.h264",
        "D:\\testFTP\\download_sync.h264");

    if (bResult)
    {
        printf("DownloadFileSync success!\n");
    }

    //system("pause");
}

void TestAsync()
{
    ProgressMonitor monitor;
    FtpClient client;
    client.RegisterObserver(&monitor);
    std::vector<std::string> vectMatch;
    vectMatch.push_back("h264");
    vectMatch.push_back("txt");
    client.UploadDirMatchedFilesAsync("ftp://192.168.1.170/test/",
        "D:\\testFTP\\", vectMatch);
    bool bResult = client.AwaitResult();
    if (bResult)
    {
        printf("UploadDirMatchedFilesAsync success!\n");
    }

    client.UploadDirAllFilesAsync("ftp://192.168.1.170/test/",
        "D:\\testFTP\\");
    bResult = client.AwaitResult();
    if (bResult)
    {
        printf("UploadDirAllFilesAsync success!\n");
    }

    client.UploadFileAsync("ftp://192.168.1.170/test/upload.h264",
        "D:\\testFTP\\upload.h264");

    Sleep(2000);
    client.Cancel();
    bResult = client.AwaitResult();
    if (!bResult)
    {
        printf("UploadFileAsync cancel success!\n");
    }

    client.DownloadFileAsync("ftp://192.168.1.170/test/upload.h264",
        "D:\\testFTP1\\download.h264");

    bResult = client.AwaitResult();
    if (bResult)
    {
        printf("DownloadFileAsync success!\n");
    }
}

int main()
{
    //TestSync();
    TestAsync();

    system("pause");
    return 0;
}