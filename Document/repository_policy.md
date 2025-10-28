# Repository Policy
_Last Update: 25.10.28._
게임태크랩 2기 week 8 team 3 저장소 정책입니다. 이후 재사용 시 팀 컨벤션에 맞게 조정하세요.

---

## 🌳 브랜치 구성
- `main`
- 작업용 브랜치: `feature/<short-desc>` 등

## 🔄 워크플로우
1. `main에서` branch off
2. 작업 수행 및 커밋
3. `main`을 대상으로 pull request(이하 PR) 생성
4. github action을 통한 빌드 테스트 통과 및 피어 리뷰
5. Merge PR

## 🛡️ 브랜치 규칙
- `main` 브랜치에는 직접 push하지 말고 PR을 이용합니다.
- PR 병합시 PR 브랜치는 항상 최신 `main`을 포함해야 합니다.
- 빌드 테스트(`build (Debug, x64)`, `build (Release, x64)`)를 모두 통과했을 때에만 PR을 병합할 수 있습니다.

---
## 📝 이슈 & 프로젝트
- Issues를 통해 작업 목록을 관리하고 있습니다. 작업 예정이거나 진행 중인 것들이 있을 때 issue에 올려두고 하는 것을 권장합니다.
- Project를 만들어 이슈의 status를 관리하고 있습니다. 작업 시작/완료시 status를 변경해주세요.