# IOCPBroadcast

IOCPNetwork DLL을 활용해 여러 TCP 클라이언트에 메시지를 전달하는 C++20 브로드캐스트 서버·클라이언트 예제 프로젝트입니다.

## 빌드 환경

- Windows x64
- Visual Studio 2026, C++ 데스크톱 개발 도구 및 Windows SDK
- MSVC v145, C++20
- IOCPNetwork의 동일 구성(Debug 또는 Release) x64 빌드 결과

Debug는 `/MDd`, Release는 `/MD`를 사용합니다. 경고 수준은 `/W4`, 소스 인코딩은 `/utf-8`이며 미리 컴파일된 헤더는 사용하지 않습니다.

## 폴더 구조

```text
IOCPBroadcast/
├─ IOCPBroadcast.slnx
├─ BroadcastServer/
│  ├─ BroadcastServer.vcxproj
│  └─ BroadcastServer.cpp
└─ BroadcastClient/
   ├─ BroadcastClient.vcxproj
   └─ BroadcastClient.cpp
```

## 저장소 배치 및 의존성

엔진 소스와 바이너리는 이 저장소에 포함하지 않습니다. 프로젝트는 다음 상대 경로 배치를 기준으로 합니다.

```text
Workspace/
├─ Applications/
│  └─ IOCPBroadcast/
└─ Module/
   └─ IOCPNetwork/
      ├─ IOCPNetwork/
      │  ├─ IOCPServer.h
      │  └─ IOCPClient.h
      └─ x64/
         ├─ Debug/    # IOCPNetwork.lib, IOCPNetwork.dll, IOCPNetwork.pdb
         └─ Release/  # IOCPNetwork.lib, IOCPNetwork.dll, IOCPNetwork.pdb
```

LIB는 링크 시 엔진 출력 폴더에서 직접 참조합니다. DLL과 PDB는 빌드 후 각 EXE 폴더로 복사합니다. 소비 프로젝트에는 `IOCPNETWORK_EXPORTS`를 정의하지 않습니다.

배치를 변경하면 두 프로젝트의 다음 설정을 함께 수정해야 합니다.

- C/C++ → 일반 → 추가 포함 디렉터리
- 링커 → 일반 → 추가 라이브러리 디렉터리
- 빌드 이벤트 → 빌드 후 이벤트 → DLL·PDB 복사 명령

## 빌드 및 실행

1. IOCPNetwork 솔루션을 원하는 구성의 x64로 먼저 빌드합니다.
2. `IOCPBroadcast.slnx`를 Visual Studio에서 엽니다.
3. 엔진과 동일한 Debug 또는 Release 구성 및 x64 플랫폼을 선택합니다.
4. 솔루션을 빌드합니다.
5. 실행할 서버 또는 클라이언트를 시작 프로젝트로 설정합니다.

출력 디렉터리는 프로젝트별로 분리합니다.

```text
Build/x64/<Configuration>/
├─ BroadcastServer/
│  ├─ BroadcastServer.exe
│  ├─ IOCPNetwork.dll
│  └─ IOCPNetwork.pdb
└─ BroadcastClient/
   ├─ BroadcastClient.exe
   ├─ IOCPNetwork.dll
   └─ IOCPNetwork.pdb
```

중간 디렉터리는 `Build/Intermediate/<ProjectName>/x64/<Configuration>/`입니다. EXE 자체의 디버깅용 PDB도 해당 출력 폴더에 생성됩니다.

엔진은 이 솔루션에서 자동으로 빌드하지 않습니다. 엔진을 수정했다면 엔진을 먼저 빌드하고 예제 프로젝트를 다시 빌드하여 DLL·PDB를 갱신하세요.
