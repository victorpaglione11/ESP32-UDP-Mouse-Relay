#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <thread>
#include <atomic>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

constexpr char TARGET_IP[] = "";
constexpr uint16_t TARGET_PORT = 5000;
constexpr UINT ID_BUTTON_CLOSE = 1001;

#pragma pack(push, 1)
struct MousePacket
{
	int16_t dx;
	int16_t dy;
	int8_t scroll;
	uint8_t buttons;
};
#pragma pack(pop)
struct ButtonMap
{
	USHORT downFlag;
	USHORT upFlag;
	uint8_t bitMask;
};

const ButtonMap BUTTON_MAPPINGS[] =
{
	{ RI_MOUSE_LEFT_BUTTON_DOWN,   RI_MOUSE_LEFT_BUTTON_UP,   0x01 },
	{ RI_MOUSE_RIGHT_BUTTON_DOWN,  RI_MOUSE_RIGHT_BUTTON_UP,  0x02 },
	{ RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_UP, 0x04 },
	{ RI_MOUSE_BUTTON_1_DOWN,      RI_MOUSE_BUTTON_1_UP,      0x08 },
	{ RI_MOUSE_BUTTON_2_DOWN,      RI_MOUSE_BUTTON_2_UP,      0x10 }
};

SOCKET g_udpSocket = INVALID_SOCKET;
sockaddr_in g_esp32Addr{};

std::atomic<int16_t> g_moveX{ 0 };
std::atomic<int16_t> g_moveY{ 0 };
std::atomic<int8_t>  g_wheel{ 0 };
std::atomic<uint8_t> g_buttons{ 0 };
std::atomic<bool>    g_buttonChanged{ false };
std::atomic<bool>    g_running{ true };

bool InitUDP()
{
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

	g_udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (g_udpSocket == INVALID_SOCKET)
	{
		WSACleanup();
		return false;
	}

	int sndBuf = 64 * 1024;
	int sendTimeout = 1000;
	setsockopt(g_udpSocket, SOL_SOCKET, SO_SNDBUF, (char*)&sndBuf, sizeof(sndBuf));
	setsockopt(g_udpSocket, SOL_SOCKET, SO_SNDTIMEO, (char*)&sendTimeout, sizeof(sendTimeout));

	g_esp32Addr.sin_family = AF_INET;
	g_esp32Addr.sin_port = htons(TARGET_PORT);

	if (inet_pton(AF_INET, TARGET_IP, &g_esp32Addr.sin_addr) != 1)
	{
		closesocket(g_udpSocket);
		WSACleanup();
		return false;
	}

	return true;
}

void SendPacket()
{
	int16_t dx = g_moveX.exchange(0);
	int16_t dy = g_moveY.exchange(0);
	int8_t scroll = g_wheel.exchange(0);
	bool changed = g_buttonChanged.exchange(false);

	if (dx == 0 && dy == 0 && scroll == 0 && !changed)
	{
		return;
	}

	MousePacket packet{ dx, dy, scroll, g_buttons.load() };

	sendto(
		g_udpSocket,
		reinterpret_cast<const char*>(&packet),
		sizeof(packet),
		0,
		reinterpret_cast<sockaddr*>(&g_esp32Addr),
		sizeof(g_esp32Addr)
	);
}

void NetworkThread()
{
	while (g_running)
	{
		SendPacket();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

bool RegisterRawMouse(HWND hwnd)
{
	RAWINPUTDEVICE rid{};
	rid.usUsagePage = 0x01;
	rid.usUsage = 0x02;
	rid.dwFlags = RIDEV_INPUTSINK;
	rid.hwndTarget = hwnd;

	return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

void ProcessRawInput(LPARAM lParam)
{
	UINT dwSize = 0;
	GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &dwSize, sizeof(RAWINPUTHEADER));

	if (dwSize == 0)
		return;

	alignas(RAWINPUT) BYTE rawBuffer[sizeof(RAWINPUT) + 64];

	if (dwSize > sizeof(rawBuffer))
		return;

	if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, rawBuffer, &dwSize, sizeof(RAWINPUTHEADER)) == (UINT)-1)
	{
		return;
	}

	RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(rawBuffer);

	if (raw->header.dwType != RIM_TYPEMOUSE)
		return;

	const RAWMOUSE& mouse = raw->data.mouse;

	if (mouse.lLastX != 0 || mouse.lLastY != 0)
	{
		g_moveX += static_cast<int16_t>(mouse.lLastX);
		g_moveY += static_cast<int16_t>(mouse.lLastY);
	}

	if (mouse.usButtonFlags & RI_MOUSE_WHEEL)
	{
		g_wheel += static_cast<int8_t>(static_cast<SHORT>(mouse.usButtonData) / WHEEL_DELTA);
	}

	const USHORT flags = mouse.usButtonFlags;
	uint8_t currentButtons = g_buttons.load();
	bool updated = false;

	for (const auto& btn : BUTTON_MAPPINGS)
	{
		if (flags & btn.downFlag)
		{
			currentButtons |= btn.bitMask;
			updated = true;
		}
		else if (flags & btn.upFlag)
		{
			currentButtons &= ~btn.bitMask;
			updated = true;
		}
	}

	if (updated)
	{
		g_buttons.store(currentButtons);
		g_buttonChanged.store(true);
	}
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	switch (msg)
	{
	case WM_CREATE:
		CreateWindow(
			L"BUTTON", L"FECHAR PROGRAMA",
			WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
			150, 180, 200, 50,
			hwnd, (HMENU)ID_BUTTON_CLOSE, GetModuleHandle(nullptr), nullptr
		);

		CreateWindow(
			L"STATIC",
			L"Mouse UDP Client - ESP32\r\n\r\n"
			L"Clique no botão para fechar\r\n\r\n",
			WS_VISIBLE | WS_CHILD | SS_CENTER,
			30, 20, 440, 140,
			hwnd, nullptr, GetModuleHandle(nullptr), nullptr
		);
		break;

	case WM_COMMAND:
		if (LOWORD(wParam) == ID_BUTTON_CLOSE) 
		{
			DestroyWindow(hwnd);
		}
		break;

	case WM_INPUT:
		ProcessRawInput(lParam);
		break;

	case WM_DESTROY:
		g_running = false;
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) 
{
	if (!InitUDP()) 
	{
		MessageBox(nullptr, L"Erro ao inicializar UDP", L"Erro", MB_OK | MB_ICONERROR);
		return 1;
	}

	std::thread netThread(NetworkThread);

	WNDCLASS wc{};
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.lpszClassName = L"RawMouseClient";

	RegisterClass(&wc);

	HWND hwnd = CreateWindowEx(
		0, wc.lpszClassName, L"Mouse UDP Client - ESP32",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT, 500, 300,
		nullptr, nullptr, hInstance, nullptr
	);

	if (!hwnd) 
	{
		g_running = false;
		if (netThread.joinable()) netThread.join();
		closesocket(g_udpSocket);
		WSACleanup();
		return 1;
	}

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);

	SetPriorityClass(GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS);
	RegisterRawMouse(hwnd);

	MSG msg{};
	while (GetMessage(&msg, nullptr, 0, 0)) 
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	g_running = false;
	if (netThread.joinable()) 
	{
		netThread.join();
	}

	closesocket(g_udpSocket);
	WSACleanup();

	return 0;
}