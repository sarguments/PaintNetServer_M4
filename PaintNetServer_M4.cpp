#include "stdafx.h"

// ProcRecv
// ProcSend
	// SendPacket_Uni
	// SendPacket_Broad(제외옵션)

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

HWND g_hWnd;
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

// FUNC //
int NetworkProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
int AcceptProc(void);

// 클라이언트 소켓 정보 추가
int AddClinetInfo(SOCKET sock, SOCKADDR_IN* addr);

// 해당 소켓에 해당하는 클라이언트 인덱스 리턴
int FindClient(WPARAM wParam);

// 클라 정보 정리
int ReleaseClient(WPARAM wParam);

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
    if (!InitInstance (hInstance, nCmdShow))
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
	if (ret_bind == NOERROR)
	{
		CCmdStart::CmdDebugText(L"bind()", false);
		return -1;
	}

	// WSAAsyncSelect
	// ACCEPT 만 등록
	int ret_select = WSAAsyncSelect(g_listenSock, g_hWnd, UM_NETWORK, FD_ACCEPT);
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

    return (int) msg.wParam;
}


int NetworkProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (WSAGETSELECTERROR(lParam))
	{
		CCmdStart::CmdDebugText(L"WSAGETSELECTERROR", false);
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

			// TODO : 서버 종료
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

		// TODO : 센드 큐에 있는거 다 보내기
	}
	break;
	case FD_READ:
	{
		// TODO : 0 리시브 하면 종료 처리
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
	InetNtop(AF_INET, &localAddr.sin_addr.s_addr, addrText, 20);
	wcout << L"Connected Client IP : " << addrText << L" // Port : " << ntohs(localAddr.sin_port) << endl;

	// 클라이언트 정보 추가
	int retval = AddClinetInfo(localSocket, &localAddr);
	if (retval < 0)
	{
		CCmdStart::CmdDebugText(L"AddClinetInfo", false);
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

	// 접속 정보 출력
	WCHAR addrText[20] = {};
	InetNtop(AF_INET, &((g_clientInfo[clientIdx]._addr).sin_addr.s_addr), addrText, 20);
	wcout << L"DISCONNECTED Client IP : " << addrText 
		<< L" // Port : " << ntohs((g_clientInfo[clientIdx]._addr).sin_port) << endl;

	g_clientInfo[clientIdx]._sock = INVALID_SOCKET;
	(g_clientInfo[clientIdx]._recvQ).ClearBuffer();
	(g_clientInfo[clientIdx]._sendQ).ClearBuffer();
	g_clientInfo[clientIdx]._sendFlag = false;
	ZeroMemory(&g_clientInfo[clientIdx]._addr, sizeof(SOCKADDR));

	return 0;
}

ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_PAINTNETSERVERM4));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = MAKEINTRESOURCEW(IDC_PAINTNETSERVERM4);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}


BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // 인스턴스 핸들을 전역 변수에 저장합니다.

   g_hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!g_hWnd)
   {
      return FALSE;
   }

   ShowWindow(g_hWnd, nCmdShow);
   UpdateWindow(g_hWnd);

   return TRUE;
}


LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
	case UM_NETWORK:
	{
		int retval = NetworkProc(hWnd, message, wParam, lParam);
		if (retval < 0)
		{
			exit(1);
		}
	}
	break;
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
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}