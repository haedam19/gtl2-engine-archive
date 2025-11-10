#pragma once
#include <fbxsdk.h>

inline bool LoadScene(const char* lFilename)
{
    // 1. FbxManager: SDK의 메모리 관리자. 가장 먼저 생성.
    FbxManager* lSdkManager = FbxManager::Create(); // [33]
    if (!lSdkManager) { return false; }

    // 2. FbxIOSettings: 임포트/익스포트 설정 관리
    FbxIOSettings* ios = FbxIOSettings::Create(lSdkManager, IOSROOT);
    lSdkManager->SetIOSettings(ios);

    // 3. FbxImporter: 실제 파일 임포터
    FbxImporter* lImporter = FbxImporter::Create(lSdkManager, ""); // [33]

    // 4. 파일 초기화 및 로드 시도
    if (!lImporter->Initialize(lFilename, -1, lSdkManager->GetIOSettings())) {
        //... 오류 처리...
        lImporter->Destroy();
        lSdkManager->Destroy();
        return false;
    }

    // 5. FbxScene: 파일의 모든 데이터를 담을 컨테이너
    FbxScene* lScene = FbxScene::Create(lSdkManager, "myScene"); // 

    // 6. 씬으로 파일 컨텐츠를 임포트
    lImporter->Import(lScene); // 

    // 7. 임포터는 더 이상 필요 없으므로 즉시 파괴 (메모리 절약)
    lImporter->Destroy(); // 

    // --- (이제 lScene 객체를 파싱하고 처리합니다) ---
    //... (3부, 4부의 내용)...

    // 8. 모든 처리가 끝나면 씬과 매니저를 파괴
    lScene->Destroy();
    lSdkManager->Destroy(); // 

    return true;
}