-- Template Lua Script
-- 이 파일은 새로운 스크립트를 생성할 때 복사
-- obj: 이 스크립트를 소유한 Actor 객체

function BeginPlay()
    print("[BeginPlay] Script started")
    PlayerController:GetPlayerCameraManager():SetViewTarget(obj, 0)
    -- 예시: Actor의 위치 출력
    local pos = obj:GetActorLocation()
    print(string.format("Initial Position: (%.2f, %.2f, %.2f)", pos.x, pos.y, pos.z))
end

function Tick(dt)
    -- 매 프레임 호출
    -- dt: Delta Time (초 단위)


end

function EndPlay()
    print("[EndPlay] Script ended")
end

-- 커스텀 이벤트 (Overlap)
function OnOverlap(OtherActor)
    print("[OnOverlap] Overlapped with: " .. tostring(OtherActor:GetName()))
end
