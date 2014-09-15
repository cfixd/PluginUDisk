#include <Windows.h>
#include "../../API/RainmeterAPI.h"
#include <vector>
#include <setupapi.h>
#include <cfgmgr32.h>
#pragma comment(lib, "setupapi.lib")

enum MeasureType
{
	All,
	UDisk,
	Removable
};

enum EjectMode
{
	Normal,
	MultiThread,
	UDiskOnly
};

struct ChildMeasure;

struct Measure
{
	MeasureType type;

	void* skin;
	LPCWSTR name;
	ChildMeasure* ownerChild;

	EjectMode mode;
	BOOL silent;
	DWORD nRetry;
	DWORD nRetryDelay;
	DWORD nDriveMask;
	DWORD nDriveExclude;
	WCHAR sDriveString[27];
	int iDriveCount;

	Measure() : type(Removable), skin(), name(), ownerChild(), mode(MultiThread), silent(true), nRetry(0), nRetryDelay(500), nDriveMask(0), nDriveExclude(0), iDriveCount(0){}

	void GetDisks(void);
	BOOL EjectDrive(WCHAR DriveLetter);
	BOOL IsMobileHDD(WCHAR cDriveLetter);

private:

	DEVINST GetDrivesDevInstByDeviceNumber(long DeviceNumber, UINT DriveType, WCHAR* szDosDeviceName);
};

struct ChildMeasure
{
	Measure* parent;
	BYTE bDrive;
	BYTE bSpace;
	BYTE bLabel;
	WCHAR sDriveRoot[3];
	std::wstring sVolume;

	ChildMeasure() : parent(), bDrive(), bSpace(), bLabel() {}

	double GetSpace(void);
	void GetLabel(void);
};

std::vector<Measure*> g_ParentMeasures;

double ChildMeasure::GetSpace(void)
{
	Measure* measure = parent;

	sDriveRoot[0] = NULL;

	if (measure->iDriveCount >= bDrive)
	{
		sDriveRoot[0] = measure->sDriveString[bDrive - 1];
		if (!bSpace) return 1;
	}
	else return 0;

	ULONGLONG nFreeBytes = 0;
	ULONGLONG nTotalBytes = 0;

	if (GetDiskFreeSpaceEx(sDriveRoot, nullptr, (PULARGE_INTEGER)&nTotalBytes, (PULARGE_INTEGER)&nFreeBytes))
	{
		if (bSpace == 1)return (double)(__int64)nFreeBytes;
		else if (bSpace == 2)return (double)(__int64)(nTotalBytes - nFreeBytes);
		else if (bSpace == 3)return (double)(__int64)nTotalBytes;
	}

	return -1;
};

void ChildMeasure::GetLabel(void)
{
	sVolume.clear();

	if (bLabel && sDriveRoot[0])
	{
		WCHAR sVolumeName[MAX_PATH + 1];

		if (GetVolumeInformation(sDriveRoot, sVolumeName, MAX_PATH + 1, nullptr, nullptr, nullptr, nullptr, 0))
		{
			if (bLabel == 1) sVolume = sVolumeName;
			else
			{
				sVolume += sDriveRoot;
				sVolume += L" ";
				sVolume += sVolumeName;
			}
		}
	}
	else
	{
		sVolume += sDriveRoot[0];
	}
};

BOOL Measure::IsMobileHDD(WCHAR cDriveLetter)
{
	HANDLE hDevice;         // 设备句柄

	WCHAR szVolumeAccessPath[] = L"\\\\.\\X:";
	szVolumeAccessPath[4] = cDriveLetter;

	// 打开设备
	hDevice = ::CreateFile(szVolumeAccessPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, OPEN_EXISTING, 0, NULL);

	if (hDevice == INVALID_HANDLE_VALUE)
	{
		return FALSE;
	}

	STORAGE_PROPERTY_QUERY Query; // input param for query
	Query.PropertyId = StorageDeviceProperty;
	Query.QueryType = PropertyStandardQuery;

	DWORD dwOutBytes; // IOCTL output length
	STORAGE_DEVICE_DESCRIPTOR pDevDesc;
	pDevDesc.Size = sizeof(STORAGE_DEVICE_DESCRIPTOR);

	if(!::DeviceIoControl(hDevice, // device handle
		IOCTL_STORAGE_QUERY_PROPERTY, // info of device property
		&Query, sizeof(STORAGE_PROPERTY_QUERY), // input data buffer
		&pDevDesc, pDevDesc.Size, // output data buffer
		&dwOutBytes, // out's length
		(LPOVERLAPPED)NULL))return false;

	UINT Type = pDevDesc.BusType;

	// http://msdn.microsoft.com/en-us/library/windows/desktop/ff800833(v=vs.85).aspx

	CloseHandle(hDevice);

	if (Type == BusTypeUsb)	return true;
	else return false;
}

void Measure::GetDisks(void)
{
	// http://msdn.microsoft.com/en-us/library/aa364939.aspx
	// http://liuviphui.blog.163.com/blog/static/202273084201331811118105/

	DWORD nMask = GetLogicalDrives() & nDriveExclude;

	if (nMask == nDriveMask)	return;
	else nDriveMask = nMask;

	WCHAR sDriveRoot[5] = L"C://";
	WCHAR sDrive[27] = {NULL};
	int iDrive = 0;

	for (int i = 0; i<26; i++)
	{
		if (nMask & 1)
		{
			sDriveRoot[0] = 'A' + i;
			if (type == All || (GetDriveType(sDriveRoot) == DRIVE_REMOVABLE)
				|| (type == Removable && IsMobileHDD(sDriveRoot[0])))
			{
				sDrive[iDrive] = sDriveRoot[0];
				iDrive++;
			}
		}
		nMask >>= 1;
	}
	wcscpy_s(sDriveString, sDrive);
	iDriveCount = iDrive;
}

// http://www.cnblogs.com/xuesong/archive/2010/08/10/1796678.html

// Use the code project:
// http://www.codeproject.com/Articles/13839/How-to-Prepare-a-USB-Drive-for-Safe-Removal

DEVINST Measure::GetDrivesDevInstByDeviceNumber(long DeviceNumber, UINT DriveType, WCHAR* szDosDeviceName)
{
	//bool IsFloppy = (strstr(szDosDeviceName, "\\Floppy") != NULL); // is there a better way?
	bool IsFloppy = false;

	GUID* guid;

	switch (DriveType) {
	case DRIVE_REMOVABLE:
		if (IsFloppy) guid = (GUID*)&GUID_DEVINTERFACE_FLOPPY;
		else guid = (GUID*)&GUID_DEVINTERFACE_DISK;
		break;
	case DRIVE_FIXED:
		guid = (GUID*)&GUID_DEVINTERFACE_DISK;
		break;
	case DRIVE_CDROM:
		guid = (GUID*)&GUID_DEVINTERFACE_CDROM;
		break;
	default:
		return 0;
	}

	// Get device interface info set handle
	// for all devices attached to system
	HDEVINFO hDevInfo = SetupDiGetClassDevs(guid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (hDevInfo == INVALID_HANDLE_VALUE)  {
		return 0;
	}
	// Retrieve a context structure for a device interface
	// of a device information set.
	DWORD dwIndex = 0;
	BOOL bRet = FALSE;

	BYTE Buf[1024];
	PSP_DEVICE_INTERFACE_DETAIL_DATA pspdidd = (PSP_DEVICE_INTERFACE_DETAIL_DATA)Buf;
	SP_DEVICE_INTERFACE_DATA         spdid;
	SP_DEVINFO_DATA                  spdd;
	DWORD                            dwSize;

	spdid.cbSize = sizeof(spdid);

	while (true)  {
		bRet = SetupDiEnumDeviceInterfaces(hDevInfo, NULL, guid, dwIndex, &spdid);
		if (!bRet) {
			break;
		}
		dwSize = 0;
		SetupDiGetDeviceInterfaceDetail(hDevInfo, &spdid, NULL, 0, &dwSize, NULL);

		if (dwSize != 0 && dwSize <= sizeof(Buf)) {
			pspdidd->cbSize = sizeof(*pspdidd); // 5 Bytes!

			ZeroMemory((PVOID)&spdd, sizeof(spdd));
			spdd.cbSize = sizeof(spdd);

			long res = SetupDiGetDeviceInterfaceDetail(hDevInfo, &spdid, pspdidd, dwSize, &dwSize, &spdd);
			if (res) {
				HANDLE hDrive = CreateFile(pspdidd->DevicePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
				if (hDrive != INVALID_HANDLE_VALUE) {
					STORAGE_DEVICE_NUMBER sdn;
					DWORD dwBytesReturned = 0;
					res = DeviceIoControl(hDrive, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn),
						&dwBytesReturned, NULL);
					if (res) {
						if (DeviceNumber == (long)sdn.DeviceNumber) {
							CloseHandle(hDrive);
							SetupDiDestroyDeviceInfoList(hDevInfo);

							return spdd.DevInst;
						}
					}
					CloseHandle(hDrive);
				}
			}
		}
		dwIndex++;
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);
	return 0;
}

BOOL Measure::EjectDrive(WCHAR DriveLetter)
{
	if (DriveLetter < 'A' || DriveLetter > 'Z') {
		return false;
	}

	// "X:\"    -> for GetDriveType
	WCHAR szRootPath[] = L"X:\\";
	szRootPath[0] = DriveLetter;

	// "X:"     -> for QueryDosDevice
	WCHAR szDevicePath[] = L"X:";
	szDevicePath[0] = DriveLetter;

	// "\\.\X:" -> to open the volume
	WCHAR szVolumeAccessPath[] = L"\\\\.\\X:";
	szVolumeAccessPath[4] = DriveLetter;

	long DeviceNumber = -1;

	HANDLE hVolume = CreateFile(szVolumeAccessPath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
	if (hVolume == INVALID_HANDLE_VALUE) {
		return false;
	}

	STORAGE_DEVICE_NUMBER sdn;
	DWORD dwBytesReturned = 0;
	long res = DeviceIoControl(hVolume, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn), &dwBytesReturned, NULL);
	if (res) {
		DeviceNumber = sdn.DeviceNumber;
	}
	CloseHandle(hVolume);

	if (DeviceNumber == -1) {
		return false;
	}

	UINT DriveType = GetDriveType(szRootPath);

	// get the dos device name (like \device\floppy0)
	// to decide if it's a floppy or not
	WCHAR szDosDeviceName[MAX_PATH];
	res = QueryDosDevice(szDevicePath, szDosDeviceName, MAX_PATH);
	if (!res) {
		return false;
	}

	DEVINST DevInst = GetDrivesDevInstByDeviceNumber(DeviceNumber, DriveType, szDosDeviceName);
	if (!DevInst) {
		return false;
	}

	// get drives's parent, e.g. the USB bridge,
	// the SATA port, an IDE channel with two drives!

	DEVINST DevInstParent = 0;
	DWORD nStatus;
	DWORD nProblemNumber;

	res = CM_Get_DevNode_Status(&nStatus, &nProblemNumber, DevInst, 0);

	if ((DN_REMOVABLE & nStatus) == 0)
	{
		res = CM_Get_Parent(&DevInstParent, DevInst, 0);
	}
	else DevInstParent = DevInst;	// SD Card

	for (DWORD tries = 1; tries <= nRetry + 1; tries++) {
		// sometimes we need some tries...

		if (silent){

			PNP_VETO_TYPE VetoType = PNP_VetoTypeUnknown;
			WCHAR VetoNameW[MAX_PATH] = {0};
			res = ::CM_Request_Device_EjectW(DevInstParent, &VetoType, VetoNameW, MAX_PATH, 0);
		}
		else{
			res = ::CM_Request_Device_EjectW(DevInstParent, NULL, NULL, 0, 0);// with messagebox (W2K, Vista) or balloon (XP)
		}

		if (res == CR_SUCCESS)  {
			return true;
		}
		Sleep(nRetryDelay);		// required to give the next tries a chance!
	}
	return false;
}

void EjectUDisk(WCHAR driveletter,BOOL eject)
{
	// http://support.microsoft.com/kb/165721/zh-cn
	// http://bbs.csdn.net/topics/90082287

	WCHAR sDevicePath[16] = L"\\\\.\\?:";
	sDevicePath[4] = driveletter;

	DWORD nRet = 0;
	HANDLE hDevice = CreateFile(sDevicePath, GENERIC_READ, FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);

	if (hDevice == INVALID_HANDLE_VALUE) return;

	if (DeviceIoControl(hDevice, FSCTL_LOCK_VOLUME, 0, 0, 0, 0, &nRet, 0)) 
	{
		if (DeviceIoControl(hDevice, FSCTL_DISMOUNT_VOLUME, 0, 0, 0, 0, &nRet, 0)) 
		{
			if (eject)DeviceIoControl(hDevice, IOCTL_STORAGE_EJECT_MEDIA, 0, 0, 0, 0, &nRet, 0);
		}
	}
	CloseHandle(hDevice);
}

typedef struct EjectData {
	WCHAR cDrive;
	void* measure;
} EJECTDATA, *PEJECTDATA;

DWORD WINAPI ThreadProc(LPVOID lpParam)
{
	PEJECTDATA data = (PEJECTDATA)lpParam;
	Measure* measure = (Measure*)data->measure;

	measure->EjectDrive(data->cDrive);

	return 0;
}

void RemoveDrive(WCHAR cDrive, void* data)
{
	Measure* measure = (Measure*)data;

	if (measure->mode == Normal)
	{
		measure->EjectDrive(cDrive);
	}
	else if (measure->mode == MultiThread)
	{
		PEJECTDATA pDataArray;
		DWORD   dwThreadIdArray;
		HANDLE  hThreadArray;

		// Allocate memory for thread data.
		pDataArray = (PEJECTDATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(EJECTDATA));

		if (pDataArray == NULL) return;

		pDataArray->cDrive = cDrive;
		pDataArray->measure = measure;

		// Create the thread to begin execution on its own.
		hThreadArray = CreateThread(
			NULL,				// default security attributes
			0,					// use default stack size  
			ThreadProc,			// thread function name
			pDataArray,			// argument to thread function 
			0,					// use default creation flags 
			&dwThreadIdArray);	// returns the thread identifier 
 
		if (hThreadArray) CloseHandle(hThreadArray);

		// I've no idea how to do the EJECTDATA clean-up......

	}
	else EjectUDisk(cDrive, true);
}

void RemoveDrives(void* data)
{
	Measure* measure = (Measure*)data;

	measure->GetDisks();
	WCHAR sDriveRoot[5] = L"C://";
	for (int i = 0; i < measure->iDriveCount; i++)
	{

		sDriveRoot[0] = measure->sDriveString[i];

		if (measure->type == All && 
			!((GetDriveType(sDriveRoot) == DRIVE_REMOVABLE) || (GetDriveType(sDriveRoot) == DRIVE_CDROM) || measure->IsMobileHDD(sDriveRoot[0])))
		{
			continue;
		}

		RemoveDrive(sDriveRoot[0], measure);
	}
}

// http://blog.csdn.net/chenyujing1234/article/details/8131533

EXTERN_C const GUID DECLSPEC_SELECTANY GUID_DEVINTERFACE_USB_DEVICE \
= { 0xA5DCBF10L, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };

BOOL EjectDevice(BOOL usbdevice)
{
	HDEVINFO hDevInfo;

	SP_DEVINFO_DATA DeviceInfoData;
	DWORD i;

	// 获取设备信息
	if (usbdevice){
		hDevInfo = SetupDiGetClassDevs((GUID*)&GUID_DEVINTERFACE_USB_DEVICE, 0, 0, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	}
	else{
		hDevInfo = SetupDiGetClassDevs((GUID*)&GUID_DEVINTERFACE_DISK, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	}

	if (hDevInfo == INVALID_HANDLE_VALUE)	return false;

	// 枚举每个USB设备
	DeviceInfoData.cbSize = sizeof(SP_DEVINFO_DATA);

	for (i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &DeviceInfoData); i++)
	{
		CONFIGRET ret;
		PNP_VETO_TYPE pnpvietotype;
		WCHAR vetoname[MAX_PATH];
		DWORD nStatus;
		DWORD nProblemNumber;

		ret = CM_Get_DevNode_Status(&nStatus, &nProblemNumber, DeviceInfoData.DevInst, 0);

		// DN_DISABLEABLE or DN_REMOVABLE
		if ((DN_DISABLEABLE & nStatus) == 0)	continue;

		if ((DN_REMOVABLE & nStatus) == 0)		continue;
		// pnpvietotype = PNP_VetoDevice;

		WCHAR info[27];
		wsprintf(info, L"U.dll: %X", DeviceInfoData.DevInst);
		RmLog(LOG_NOTICE, info);

		ret = CM_Request_Device_EjectW(DeviceInfoData.DevInst, &pnpvietotype, vetoname, MAX_PATH, 0);
	}

	if (GetLastError() != NO_ERROR &&
		GetLastError() != ERROR_NO_MORE_ITEMS)
	{
		// Insert error handling here.
		return false;
	}

	// Cleanup
	SetupDiDestroyDeviceInfoList(hDevInfo);
	return true;
}

void RemoveDevice(void)
{
	EjectDevice(true);
	EjectDevice(false);
}

PLUGIN_EXPORT void Initialize(void** data, void* rm)
{
	ChildMeasure* child = new ChildMeasure;
	*data = child;

	void* skin = RmGetSkin(rm);

	LPCWSTR parentName = RmReadString(rm, L"DriveMeasure", L"");
	if (!*parentName)
	{
		child->parent = new Measure;
		child->parent->name = RmGetMeasureName(rm);
		child->parent->skin = skin;
		child->parent->ownerChild = child;
		
		g_ParentMeasures.push_back(child->parent);
	}
	else
	{
		child->sDriveRoot[0] = NULL;
		child->sDriveRoot[1] = L':';
		child->sDriveRoot[2] = NULL;

		// Find parent using name AND the skin handle to be sure that it's the right one
		std::vector<Measure*>::const_iterator iter = g_ParentMeasures.begin();
		for (; iter != g_ParentMeasures.end(); ++iter)
		{
			if (_wcsicmp((*iter)->name, parentName) == 0 && (*iter)->skin == skin)
			{
				child->parent = (*iter);
				return;
			}
		}

		RmLog(LOG_ERROR, L"UDisk.dll: Invalid DriveMeasure");
	}
}

PLUGIN_EXPORT void Reload(void* data, void* rm, double* maxValue)
{
	ChildMeasure* child = (ChildMeasure*)data;
	Measure* measure = child->parent;

	if (!measure)	return;

	int i;
		
	if(measure->ownerChild == child) // Read parent specific options
	{
		WCHAR sOption[30] = { NULL };
		wcscpy_s(sOption, RmReadString(rm, L"Mode", L"MultiThread_Silent"));

		if (_wcsicmp(L"Normal", sOption) == 0)
		{
			measure->mode = Normal;
			measure->silent = false;
		}
		else if (_wcsicmp(L"Normal_Silent", sOption) == 0)
		{
			measure->mode = Normal;
			measure->silent = true;
		}
		else if (_wcsicmp(L"MultiThread", sOption) == 0)
		{
			measure->mode = MultiThread;
			measure->silent = false;
		}
		else if (_wcsicmp(L"MultiThread_Silent", sOption) == 0)
		{
			measure->mode = MultiThread;
			measure->silent = true;
		}
		else if (_wcsicmp(L"UDiskOnly", sOption) == 0)
		{
			measure->mode = UDiskOnly;
			measure->silent = true;
		}
		else
		{
			measure->mode = MultiThread;
			measure->silent = true;
			RmLog(LOG_ERROR, L"QuickBang.dll: Unknown Option for Measure");
		}

		wcscpy_s(sOption, RmReadString(rm, L"Drive", L"Removable"));

		if (_wcsicmp(L"All", sOption) == 0)
		{
			measure->type = All;
		}
		else if (_wcsicmp(L"UDisk", sOption) == 0)
		{
			measure->type = UDisk;
		}
		else if (_wcsicmp(L"Removable", sOption) == 0)
		{
			measure->type = Removable;
		}
		else
		{
			measure->type = Removable;
			RmLog(LOG_ERROR, L"QuickBang.dll: Unknown Option for Drive");
		}

		wcscpy_s(sOption, RmReadString(rm, L"Exclude", L""));
		_wcsupr_s(sOption, wcslen(sOption) + 1);
		DWORD n = 0;

		for (i = 0; sOption[i] != NULL; i++)
		{
			if (sOption[i] >= 'A'&& sOption[i] <= 'Z')
			{
				n |= 1 << (sOption[i] - 'A');
			}
		}
		measure->nDriveExclude = ~n;

		// Useless in most situation
		i = RmReadInt(rm, L"Retry", 0);
		measure->nRetry = i < 0 ? 0 : (i > 10 ? 10 : i);

		i = RmReadInt(rm, L"RetryDelay", 500);
		measure->nRetryDelay = i < 0 ? 0 : (i > 10000 ? 10000 : i);
	}
	else
	{
		i = RmReadInt(rm, L"Drive", 0);
		child->bDrive = i < 1 ? 1 : (i > 26 ? 26 : i);

		i = RmReadInt(rm, L"DriveSpace", 0);
		child->bSpace = i < 0 ? 0 : (i > 3 ? 3 : i);

		i = RmReadInt(rm, L"DriveLabel", 0);
		child->bLabel = i < 0 ? 0 : (i > 2 ? 2 : i);

		ULONGLONG nTotalBytes;
		if (child->bSpace && measure->iDriveCount >= child->bDrive && (child->sDriveRoot[0] = measure->sDriveString[child->bDrive - 1])
			&& GetDiskFreeSpaceEx(child->sDriveRoot, nullptr, (PULARGE_INTEGER)&nTotalBytes, nullptr))
		{
			*maxValue = (double)(__int64)nTotalBytes;
		}
		else *maxValue = 1;
	}
}

PLUGIN_EXPORT double Update(void* data)
{
	ChildMeasure* child = (ChildMeasure*)data;
	Measure* measure = child->parent;

	if (!measure)	return 0.0;

	if (measure->ownerChild == child)
	{
		measure->GetDisks();
		return measure->iDriveCount;
	}
	else
	{
		return child->GetSpace();
	}	
}

PLUGIN_EXPORT LPCWSTR GetString(void* data)
{
	ChildMeasure* child = (ChildMeasure*)data;
	Measure* measure = child->parent;

	if (measure->ownerChild == child)
	{
		return measure->sDriveString;
	}
	else
	{
		child->GetLabel();
		return child->sVolume.c_str();
	}
}

PLUGIN_EXPORT void ExecuteBang(void* data, LPCWSTR args)
{
	ChildMeasure* child = (ChildMeasure*)data;
	Measure* measure = child->parent;

	if (measure->ownerChild != child)
	{
		if (child->sDriveRoot[0] == NULL)	return;

		if (_wcsicmp(args, L"Open") == 0)
		{
			ShellExecute(NULL, L"open", child->sDriveRoot, NULL, NULL, SW_SHOWNORMAL); //SW_SHOWMAXIMIZED
		}
		else if (_wcsicmp(args, L"Remove") == 0)
		{
			RemoveDrive(child->sDriveRoot[0], measure);
		}
		else RmLog(LOG_WARNING, L"UDisk.dll: Not available for child measure");

		return;
	}

	WCHAR sBang[30] = { NULL };
	WCHAR *sFind;
	WCHAR cDrive = NULL;
	BOOL error = false;

	wcscpy_s(sBang, args);

	if (sFind = wcschr(sBang, L'_'))
	{
		swscanf_s(sFind + 1, L"%c", &cDrive);
		*sFind = NULL;
	}

	if (_wcsicmp(sBang, L"RemoveAll") == 0)
	{
		RemoveDevice();
	}
	else if (_wcsicmp(sBang, L"RemoveDrive") == 0)
	{
		RemoveDrives(measure);
	}
	else if (_wcsicmp(sBang, L"Remove") == 0)
	{
		if (cDrive)
		{
			RemoveDrive(cDrive, measure);
		}
		else error = true;
	}
	else if (_wcsicmp(sBang, L"DisMount") == 0)
	{
		if (cDrive)
		{
			EjectUDisk(cDrive, false);
		}
		else error = true;
	}
	else RmLog(LOG_WARNING, L"UDisk.dll: Unknown Bang");

	if (error) RmLog(LOG_WARNING, L"UDisk.dll: Unknown Option for Bang");

}

PLUGIN_EXPORT void Finalize(void* data)
{
	ChildMeasure* child = (ChildMeasure*)data;
	Measure* parent = child->parent;

	if (parent && parent->ownerChild == child)
	{
		delete parent;

		std::vector<Measure*>::iterator iter = std::find(g_ParentMeasures.begin(), g_ParentMeasures.end(), parent);
		g_ParentMeasures.erase(iter);
	}

	delete child;
}
