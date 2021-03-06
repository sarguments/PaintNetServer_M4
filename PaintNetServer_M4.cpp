#include "stdafx.h"

// TODO : 큐에 있는거 보내기 함수화

#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include "PaintNetServer_M4.h"
#include "hoxy_Header.h"
#include "RingBuffer_AEK999.h"

#define MAX_LOADSTRING 100

#define SERVERPORT 25000
#define HEADERSIZE 2
#define UM_NETWORK (WM_USER + 1)
#define ARR_SIZE 1000

// 전역 변수:
HINSTANCE hInst;                                // 현재 인스턴스입니다.
WCHAR szTitle[MAX_LOADSTRING];                  // 제목 표시줄 텍스트입니다.
WCHAR szWindowClass[MAX_LOADSTRING];            // 기본 창 클래스 이름입니다.

//HWND g_hWnd;
HWND g_hMDlog;

SOCKET g_listenSock = INVALID_SOCKET;
SOCKADDR_IN g_sockAddr;

// 클라 정보 구조체
struct st_ClientInfo
{
	SOCKET _sock = INVALID_SOCKET;
	CRingBuffer _sendQ;
	CRingBuffer _recvQ;
	bool _sendFlag = false;
	SOCKADDR_IN _addr;
};

st_ClientInfo g_clientInfo[50];

// 다이얼로그
LRESULT CALLBACK DialogProc(HWND hDlg, UINT iMessage, WPARAM wParam, LPARAM lParam);

// FUNC //
int NetworkProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
int AcceptProc(void);

// 클라이언트 소켓 정보 추가
int AddClinetInfo(SOCKET sock, SOCKADDR_IN* addr);

// 해당 소켓에 해당하는 클라이언트 인덱스 리턴
int FindClient(WPARAM wParam);

// 클라 정보 정리
int ReleaseClient(WPARAM wParam);

// SEND //
int ProcSend(void);
int SendPacket(char* buffer, int size);

// TODO : 이거 안쓰잖아
// .....진짜로 보내는 함수
int SendPacket_Uni(SOCKET sock, char* buf, int size);
int SendPacket_Broad(char* buf, int size, SOCKET sock = INVALID_SOCKET); // 인자 소켓 넣으면 그거 제외하고 보내기

// RECEV //
int ProcRead(WPARAM wParam);
int RecvPacket(SOCKET sock, char* buffer, int size);

// wouldblock 이 떴을 경우 다시 fd_write 오면 ProcSend 해서 해당 소켓 센드큐에 있는거 다보낸다.

// 이 코드 모듈에 들어 있는 함수의 정방향 선언입니다.
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	UNREFERENCED_PARAMETER(lpCmdLine);

	// 전역 문자열을 초기화합니다.
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_PAINTNETSERVERM4, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// 응용 프로그램 초기화를 수행합니다.
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_PAINTNETSERVERM4));

	//////// MAIN ////////
	CCmdStart myCmdStart;
	CSockUtill::WSAStart();

	// listen 소켓 초기화
	g_listenSock = socket(AF_INET, SOCK_STREAM, 0);
	if (g_listenSock == INVALID_SOCKET)
	{
		CCmdStart::CmdDebugText(L"socket()", false);
		return -1;
	}

	g_sockAddr.sin_family = AF_INET;
	g_sockAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	g_sockAddr.sin_port = htons(SERVERPORT);

	int ret_bind = bind(g_listenSock, (SOCKADDR*)&g_sockAddr, sizeof(SOCKADDR));
	if (ret_bind != NOERROR)
	{
		CCmdStart::CmdDebugText(L"bind()", false);
		return -1;
	}

	// WSAAsyncSelect
	// ACCEPT 만 등록
	int ret_select = WSAAsyncSelect(g_listenSock, g_hMDlog, UM_NETWORK, FD_ACCEPT);
	if (ret_select == SOCKET_ERROR)
	{
		CCmdStart::CmdDebugText(L"WSAAsyncSelect()", false);
		return -1;
	}

	// 리슨 소켓 listen
	int ret_listen = listen(g_listenSock, SOMAXCONN);
	if (ret_listen == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			CCmdStart::CmdDebugText(L"listen()", false);
			return -1;
		}
	}

	//////////////////////////////////////////////////////////////

	MSG msg;

	// 기본 메시지 루프입니다.
	while (GetMessage(&msg, nullptr, 0, 0))
	{
		if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	return (int)msg.wParam;
}

int NetworkProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (WSAGETSELECTERROR(lParam))
	{
		CCmdStart::CmdDebugText(L"WSAGETSELECTERROR", false);

		int clientIdx = FindClient(wParam);

		WCHAR addrText[20] = {};
		InetNtop(AF_INET, &g_clientInfo[clientIdx]._addr.sin_addr.s_addr, addrText, 20);

		WCHAR listText[50] = {};
		wsprintf(listText, L"%ws : ERROR", addrText);

		HWND localHwnd = GetDlgItem(g_hMDlog, IDC_LIST1);
		SendMessage(localHwnd, LB_ADDSTRING, 0, (LPARAM)listText);

		return -1;
	}

	switch (WSAGETSELECTEVENT(lParam))
	{
		// 통신을 위한 연결 절차가 끝났다.
	case FD_ACCEPT:
	{
		// accept == invalidSock 뜨면 비정상. 서버종료.
		int retval = AcceptProc();
		if (retval < 0)
		{
			CCmdStart::CmdDebugText(L"AcceptProc", false);

			exit(1);
		}
	}
	break;
	case FD_WRITE:
	{
		int clientIdx = FindClient(wParam);
		if (clientIdx == -1)
		{
			CCmdStart::CmdDebugText(L"FD_WRITE", false);
			return -1;
		}

		g_clientInfo[clientIdx]._sendFlag = true;

		ProcSend();
	}
	break;
	case FD_READ:
	{
		int retval = ProcRead(wParam);
		if (retval == -1)
		{
			ReleaseClient(wParam);
		}

		ProcSend();
	}
	break;
	case FD_CLOSE:
	{
		int retval = ReleaseClient(wParam);
		if (retval < 0)
		{
			return -1;
		}

		return 0;
	}
	break;
	}

	return 0;
}

int AcceptProc(void)
{
	SOCKET localSocket = INVALID_SOCKET;
	SOCKADDR_IN localAddr;
	int addrSize = sizeof(localAddr);

	localSocket = accept(g_listenSock, (SOCKADDR*)&localAddr, &addrSize);
	if (localSocket == INVALID_SOCKET)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			CCmdStart::CmdDebugText(L"accept()", false);
			return -1;
		}
	}

	// 접속 정보 출력
	WCHAR addrText[20] = {};
	InetNtop(AF_INET, &localAddr.sin_addr.s_addr, addrText, 50);

	wcout << L"Connected Client IP : " << addrText << L" // Port : " << ntohs(localAddr.sin_port) << endl;

	WCHAR listText[50] = {};
	wsprintf(listText, L"%ws is connected", addrText);

	HWND localHwnd = GetDlgItem(g_hMDlog, IDC_LIST1);
	SendMessage(localHwnd, LB_ADDSTRING, 0, (LPARAM)listText);

	// 클라이언트 정보 추가
	int retval = AddClinetInfo(localSocket, &localAddr);
	if (retval < 0)
	{
		CCmdStart::CmdDebugText(L"AddClinetInfo", false);
		return -1;
	}

	// 메세지 따로 지정해도 됨
	int ret_select = WSAAsyncSelect(localSocket, g_hMDlog, UM_NETWORK, FD_WRITE | FD_READ | FD_CLOSE);
	if (ret_select == SOCKET_ERROR)
	{
		CCmdStart::CmdDebugText(L"WSAAsyncSelect()", false);
		return -1;
	}

	return 0;
}

int AddClinetInfo(SOCKET sock, SOCKADDR_IN* addr)
{
	for (int i = 0; i < 50; i++)
	{
		if (g_clientInfo[i]._sock != INVALID_SOCKET)
		{
			continue;
		}

		g_clientInfo[i]._sock = sock;
		memcpy(&g_clientInfo[i]._addr, addr, sizeof(SOCKADDR));
		return 0;
	}

	return -1;
}

int FindClient(WPARAM wParam)
{
	SOCKET localSock = (SOCKET)wParam;

	for (int i = 0; i < 50; i++)
	{
		if (g_clientInfo[i]._sock != localSock)
		{
			continue;
		}

		return i;
	}

	return -1;
}

int ReleaseClient(WPARAM wParam)
{
	int clientIdx = FindClient(wParam);
	if (clientIdx == -1)
	{
		CCmdStart::CmdDebugText(L"ReleaseClient", false);
		return -1;
	}

	// TODO : 논블럭에서 closesocket 에러처리?
	int retval = closesocket((SOCKET)wParam);
	if (retval == SOCKET_ERROR)
	{
		CCmdStart::CmdDebugText(L"closesocket()", false);
		return -1;
	}

	// 소켓 단절 정보 출력
	WCHAR addrText[20] = {};
	InetNtop(AF_INET, &((g_clientInfo[clientIdx]._addr).sin_addr.s_addr), addrText, 20);
	wcout << L"DISCONNECTED Client IP : " << addrText
		<< L" // Port : " << ntohs((g_clientInfo[clientIdx]._addr).sin_port) << endl;

	WCHAR listText[50] = {};
	wsprintf(listText, L"%ws is DISCONNECTED", addrText);

	HWND localHwnd = GetDlgItem(g_hMDlog, IDC_LIST1);
	SendMessage(localHwnd, LB_ADDSTRING, 0, (LPARAM)listText);

	g_clientInfo[clientIdx]._sock = INVALID_SOCKET;
	(g_clientInfo[clientIdx]._recvQ).ClearBuffer();
	(g_clientInfo[clientIdx]._sendQ).ClearBuffer();
	g_clientInfo[clientIdx]._sendFlag = false;
	ZeroMemory(&g_clientInfo[clientIdx]._addr, sizeof(SOCKADDR));

	return 0;
}

// 돌면서 센드큐 비우기
int ProcSend(void)
{
	for (int i = 0; i < 50; i++)
	{
		// sendFalg == false 인거 패스
		if (g_clientInfo[i]._sendFlag == false)
		{
			continue;
		}

		while (1)
		{
			// 센드큐에 있는거 다 보낸다
			int inUseSize = (g_clientInfo[i]._sendQ).GetUseSize();
			if (inUseSize < HEADERSIZE)
			{
				break;
			}

			// 먼저 헤더를 본다
			unsigned short packetHeader;
			(g_clientInfo[i]._sendQ).Peek((char*)&packetHeader, HEADERSIZE);

			// 길이가 0인 경우
			if (packetHeader == 0)
			{
				ReleaseClient((WPARAM)g_clientInfo[i]._sock);
				return -1;
			}

			int packetSize = HEADERSIZE + packetHeader;

			// g_recvQ에 헤더사이즈 + 페이로드 길이 만큼 있는지
			if ((g_clientInfo[i]._sendQ).GetUseSize() < packetSize)
			{
				// 큐에 전체 패킷이 아직 다 못 들어온 경우? (계속 받는다)
				return 0;
			}

			// 일단 픽해서 센드하고 센드큐에서 보낸만큼 데이터 날림
			char localBuf[ARR_SIZE];
			(g_clientInfo[i]._sendQ).Peek((char*)localBuf, packetSize);

			int ret_send = send(g_clientInfo[i]._sock, localBuf, packetSize, 0);
			if (ret_send == SOCKET_ERROR)
			{
				if (WSAGetLastError() == WSAEWOULDBLOCK)
				{
					// TODO : 우드블럭 테스트
					// WOULDBLOCK 뜨면 sendFlag = false

					g_clientInfo[i]._sendFlag = false;
				}
				else
				{
					CCmdStart::CmdDebugText(L"send", false);

					return -1;
				}
			}

			//wcout << L"sendQ Use : " << inUseSize << L" / ret_send : " << ret_send << endl;
			(g_clientInfo[i]._sendQ).MoveFrontPos(ret_send);
		}
	}

	return 0;
}

int SendPacket(char * buffer, int size)
{
	char* pLocalBuf = buffer;
	int localSize = size;

	for (int i = 0; i < 50; i++)
	{
		if (g_clientInfo[i]._sock == INVALID_SOCKET)
		{
			continue;
		}

		if ((g_clientInfo[i]._sendQ).GetFreeSize() < localSize)
		{
			CCmdStart::CmdDebugText(L"GetFreeSize() < localSize", false);

			return -1;
		}

		int retval = (g_clientInfo[i]._sendQ).Enqueue(pLocalBuf, localSize);
		if (retval != localSize)
		{
			CCmdStart::CmdDebugText(L"retval != localSize", false);

			return -1;
		}
	}

	return 0;
}

int SendPacket_Uni(SOCKET sock, char * buf, int size)
{
	int ret_send = send(sock, buf, size, 0);
	if (ret_send == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			CCmdStart::CmdDebugText(L"send()", false);
			return -1;
		}
	}

	//wcout << L"send : " << ret_send << endl;

	return ret_send;
}

int SendPacket_Broad(char * buf, int size, SOCKET sock)
{
	// TODO : 다 돌 필요 있나?
	for (int i = 0; i < 50; i++)
	{
		if (g_clientInfo[i]._sock == INVALID_SOCKET ||
			sock == INVALID_SOCKET)
		{
			continue;
		}

		// 유니캐스트
		int retval = SendPacket_Uni(g_clientInfo[i]._sock, buf, size);
		if (retval == -1)
		{
			ReleaseClient((WPARAM)g_clientInfo[i]._sock);
		}
	}

	return 0;
}

int ProcRead(WPARAM wParam)
{
	SOCKET localSocket = (SOCKET)wParam;
	int clientIdx = FindClient(wParam);
	if (clientIdx == -1)
	{
		CCmdStart::CmdDebugText(L"FindClient()", false);
		return -1;
	}

	// 일단 리시브
	char localBuf[ARR_SIZE];
	int retval = recv(localSocket, localBuf, ARR_SIZE, 0);
	if (retval == SOCKET_ERROR)
	{
		if (WSAGetLastError() != WSAEWOULDBLOCK)
		{
			CCmdStart::CmdDebugText(L"recv()", false);
			return -1;
		}
	}

	//wcout << L"recved : " << retval << endl;

	// 소켓종료
	if (retval == 0)
	{
		wcout << L"closesocket" << endl;
		return -1;
	}

	// recv 받은만큼 넣을 공간이 있는지
	if ((g_clientInfo[clientIdx]._recvQ).GetFreeSize() < retval)
	{
		CCmdStart::CmdDebugText(L"GetFreeSize() < retval", false);
		return -1;
	}

	int ret_enqueue = (g_clientInfo[clientIdx]._recvQ).Enqueue(localBuf, retval);
	if (ret_enqueue == -1)
	{
		// 리시브큐 꽉 찬 경우
		CCmdStart::CmdDebugText(L"g_recvQ.Enqueue", false);
		return -1;
	}

	// TODO : 중복루틴?
	if (ret_enqueue != retval)
	{
		// 큐에 다 안들어 간 경우
		CCmdStart::CmdDebugText(L"ret_enqueue != retval", false);
		return -1;
	}

	while (1)
	{
		int inUseSize = (g_clientInfo[clientIdx]._recvQ).GetUseSize();
		if (inUseSize < HEADERSIZE)
		{
			break;
		}

		// 먼저 헤더를 본다
		unsigned short packetHeader;
		(g_clientInfo[clientIdx]._recvQ).Peek((char*)&packetHeader, HEADERSIZE);
		if (packetHeader == 0)
		{
			// 길이가 0인 경우
			return -1;
		}

		int packetSize = HEADERSIZE + packetHeader;

		// g_recvQ에 헤더사이즈 + 페이로드 길이 만큼 있는지
		if ((g_clientInfo[clientIdx]._recvQ).GetUseSize() < packetSize)
		{
			// 큐에 전체 패킷이 아직 다 못 들어온 경우? (계속 받는다)
			return 0;
		}

		char deqBuf[ARR_SIZE];
		int ret_dequeue = (g_clientInfo[clientIdx]._recvQ).Dequeue(deqBuf, packetSize);

		int retval = SendPacket(deqBuf, ret_dequeue);
		if (retval == -1)
		{
			CCmdStart::CmdDebugText(L"SendPacket", false);
			return -1;
		}
	}

	// ret_enqueue 리턴
	return ret_enqueue;
}

int RecvPacket(SOCKET sock, char * buffer, int size)
{
	int clientIdx = FindClient((WPARAM)sock);
	if (clientIdx == -1)
	{
		CCmdStart::CmdDebugText(L"FindClient()", false);
		return -1;
	}

	char* pLocalBuf = buffer;
	int localSize = size;

	// 넣으려는 크기보다 남은 공간이 적을 경우
	if ((g_clientInfo[clientIdx]._recvQ).GetFreeSize() < localSize)
	{
		CCmdStart::CmdDebugText(L"GetFreeSize() < localSize", false);
		return -1;
	}

	int retval = (g_clientInfo[clientIdx]._recvQ).Enqueue(pLocalBuf, localSize);

	return retval;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
	WNDCLASSEXW wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);

	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PAINTNETSERVERM4));
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_PAINTNETSERVERM4);
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

	return RegisterClassExW(&wcex);
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // 인스턴스 핸들을 전역 변수에 저장합니다.

	//g_hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
	//	CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

	//if (!g_hWnd)
	//{
	//	return FALSE;
	//}

	g_hMDlog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_DIALOG1),
		NULL, DialogProc);

	ShowWindow(g_hMDlog, SW_SHOW);

	//ShowWindow(g_hWnd, nCmdShow);
	//UpdateWindow(g_hWnd);

	return TRUE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		//case UM_NETWORK:
		//{
		//	int retval = NetworkProc(hWnd, message, wParam, lParam);
		//	if (retval < 0)
		//	{
		//		ReleaseClient(wParam);
		//	}
		//}
		//break;
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// 메뉴 선택을 구문 분석합니다.
		switch (wmId)
		{
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	break;
	case WM_PAINT:
	{
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hWnd, &ps);

		EndPaint(hWnd, &ps);
	}
	break;
	case WM_DESTROY:
		for (int i = 0; i < 50; i++)
		{
			if (g_clientInfo[i]._sock == INVALID_SOCKET)
			{
				continue;
			}

			ReleaseClient((WPARAM)g_clientInfo[i]._sock);
		}
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK DialogProc(HWND hDlg, UINT iMessage, WPARAM wParam, LPARAM lParam)
{
	switch (iMessage)
	{
	case WM_INITDIALOG:
	{
	}
	break;
	case UM_NETWORK:
	{
		int retval = NetworkProc(hDlg, iMessage, wParam, lParam);
		if (retval < 0)
		{
			ReleaseClient(wParam);
		}
	}
	break;
	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case IDCANCEL:
		{
			// TODO : 종료 어떻게?
			for (int i = 0; i < 50; i++)
			{
				if (g_clientInfo[i]._sock == INVALID_SOCKET)
				{
					continue;
				}

				ReleaseClient((WPARAM)g_clientInfo[i]._sock);
			}

			CSockUtill::CleanUp();
			exit(1);

			//return TRUE;
		}
		break;
		}
	}
	//case WM_DESTROY:
	//{
	//	for (int i = 0; i < 50; i++)
	//	{
	//		if (g_clientInfo[i]._sock == INVALID_SOCKET)
	//		{
	//			continue;
	//		}

	//		ReleaseClient((WPARAM)g_clientInfo[i]._sock);
	//		CSockUtill::CleanUp();
	//
	//	}
	//}
	//break;
	break;
	}

	return 0;
}