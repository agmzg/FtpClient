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
        printf("upload success!\n");
    }

    bResult = client.DownloadFileSync("ftp://192.168.1.170/test/upload.h264",
        "D:\\testFTP\\download_sync.h264");

    if (bResult)
    {
        printf("download success!\n");
    }

    //system("pause");
}

void TestAsync()
{
    ProgressMonitor monitor;
    FtpClient client;
    client.RegisterObserver(&monitor);
//     client.UploadDirectoryAsync("ftp://192.168.1.170/test/",
//         "D:\\testFTP\\");
//     bool bResult = client.AwaitResult();
// 
//     client.UploadFileAsync("ftp://192.168.1.170/test/upload.h264",
//         "D:\\testFTP\\upload.h264");
// 
//     Sleep(2000);
//     client.Cancel();
//     bResult = client.AwaitResult();
//     if (bResult)
//     {
//         printf("upload success!\n");
//     }

    client.DownloadFileAsync("ftp://192.168.1.170/test/upload.h264",
        "D:\\testFTP1\\download.h264");

    bool bResult = client.AwaitResult();
    if (bResult)
    {
        printf("download success!\n");
    }
}

int main()
{
    //TestSync();
    TestAsync();

    system("pause");
    return 0;
}