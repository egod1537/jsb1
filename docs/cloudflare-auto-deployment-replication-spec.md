# Cloudflare 브랜치 자동 배포 재현 명세

상태: 구현 기준안  
작성일: 2026-09-05  
참조 구현: JSB1의 `deploy.sh`, `auto-deploy.sh`, `scripts/deploy-lib.sh`,
`compose.deploy.yaml`, `compose.edge.yaml`

## 1. 목적

이 문서는 JSB1에서 현재 운영 중인 배포 방식을 다른 프로젝트에 재현하기
위한 구현 명세다. 대상 방식은 Cloudflare Pages나 GitHub Actions가 애플리케이션을
빌드하는 구조가 아니다. 배포 호스트가 Git 원격 브랜치를 감시하고 직접 Docker
이미지를 빌드한 다음, 하나의 Cloudflare Tunnel을 통해 브랜치별 HTTPS 주소를
공개하는 구조다.

이 명세를 구현한 프로젝트는 다음 동작을 제공해야 한다.

- 원격 브랜치의 새 커밋을 1분 이내에 발견한다.
- 브랜치 HEAD가 아닌 확인된 전체 commit SHA를 배포 단위로 사용한다.
- 프로덕션과 브랜치 미리보기에 결정적인 호스트명을 부여한다.
- 브랜치마다 애플리케이션 컨테이너, 데이터, 포트, 상태를 격리한다.
- Caddy와 cloudflared는 프로젝트당 하나만 실행하고 모든 브랜치가 공유한다.
- 외부에서 HTTPS health check가 성공한 경우에만 배포 성공으로 기록한다.
- 삭제된 원격 브랜치는 유예 기간 후 자동으로 내리되 데이터를 보존한다.
- 직전 성공 commit으로 명시적으로 롤백할 수 있다.

## 2. 범위와 비범위

### 범위

- 단일 Linux 또는 macOS 호스트
- Docker Compose 기반 애플리케이션
- locally-managed Cloudflare Tunnel
- 동일 Cloudflare zone 안의 프로덕션 주소와 브랜치 미리보기 주소
- polling 기반 자동 배포
- 선택적인 GitHub Deployments/Commit Status 보고

### 비범위

- Cloudflare Pages/Workers 빌드
- Kubernetes 또는 다중 호스트 스케줄링
- webhook 수신 서버
- 데이터베이스 스키마 자동 롤백
- 무중단 blue-green 배포 보장
- 여러 호스트 간 상태 동기화

## 3. 목표 구조

```text
Git origin
   │  매분 ls-remote --heads
   ▼
호스트 자동 배포기
   ├── commit별 detached worktree
   ├── branch별 Docker Compose project
   ├── branch별 127.0.0.1 포트
   └── branch별 Caddy route fragment
                │
인터넷 ─ Cloudflare DNS/Universal SSL
                │
                ▼
       프로젝트 전용 Cloudflare Tunnel 1개
                │
                ▼
       프로젝트 전용 Caddy edge 1개
                │ Host 기준 reverse proxy
        ┌───────┼────────┐
        ▼       ▼        ▼
      main   feature-a  feature-b
```

브라우저 TLS는 Cloudflare가 종료한다. Cloudflare Tunnel에서 Caddy까지는 별도의
Cloudflare Origin Certificate를 사용해 HTTPS로 연결한다. 애플리케이션 컨테이너의
호스트 포트는 반드시 `127.0.0.1`에만 바인딩한다.

## 4. 프로젝트별 결정값

구현을 시작하기 전에 아래 값을 확정해야 한다. 예시는 새 프로젝트 `alpha`를
가정한다.

| 항목 | 변수 예시 | 예시 값 | 규칙 |
| --- | --- | --- | --- |
| 프로젝트 키 | `DEPLOY_PROJECT_KEY` | `alpha` | DNS/Compose에서 안전한 소문자 식별자 |
| Git 저장소 | `DEPLOY_GITHUB_REPOSITORY` | `owner/alpha` | GitHub 보고 대상 |
| 기본 도메인 | `DEPLOY_BASE_DOMAIN` | `mangagaki.net` | Cloudflare의 active zone |
| 프로덕션 브랜치 | `DEPLOY_MAIN_BRANCH` | `main` | 자동 배포 기본값은 꺼짐 |
| 프로덕션 호스트 | `DEPLOY_MAIN_HOSTNAME` | `alpha.mangagaki.net` | 기본 도메인 바로 아래 한 label |
| 미리보기 형식 | 파생값 | `<slug>-alpha.mangagaki.net` | 기본 도메인 바로 아래 한 label |
| Tunnel | `DEPLOY_CLOUDFLARE_TUNNEL` | `alpha` 또는 UUID | 다른 프로젝트와 분리 권장 |
| Compose edge project | `DEPLOY_EDGE_PROJECT` | `alpha-edge` | 호스트에서 고유해야 함 |
| 앱 Compose prefix | 파생값 | `alpha-<slug>` | 호스트에서 고유해야 함 |
| 앱 포트 범위 | `DEPLOY_PORT_START/END` | `9000-9499` | 다른 프로젝트와 겹치지 않아야 함 |
| edge HTTPS 포트 | `DEPLOY_EDGE_HTTPS_PORT` | `4444` | loopback 전용, 호스트에서 고유 |
| 상태 루트 | `DEPLOY_STATE_DIR` | `<repo>/data/branch-deployments` | Git ignore 대상 |

같은 호스트에서 JSB1과 함께 실행한다면 Tunnel, edge Compose project, 상태 루트,
포트 범위, edge loopback 포트를 새 프로젝트 전용으로 분리해야 한다. 기존 JSB1의
생성형 설정 파일을 다른 프로젝트가 함께 쓰거나 덮어써서는 안 된다.

## 5. 애플리케이션 계약

배포 자동화와 애플리케이션의 경계는 다음과 같다.

### 5.1 컨테이너 계약

- 프로젝트는 재현 가능한 Docker build를 제공해야 한다.
- 외부 진입점 역할의 `web` 서비스 하나를 제공해야 한다.
- `web`만 `127.0.0.1:${DEPLOY_PORT}:<container-port>`로 publish해야 한다.
- DB, API, worker 등의 내부 포트를 LAN 또는 모든 인터페이스에 publish하면 안 된다.
- 장기 실행 서비스는 `restart: unless-stopped`를 사용해야 한다.
- 로그는 최소 `max-size: 10m`, `max-file: 5`로 회전해야 한다.
- branch-private 영속 데이터는 상태 루트의 `units/<slug>/data`에 bind mount하거나
  branch-private named volume을 사용해야 한다.

### 5.2 HTTP 계약

각 배포는 다음 endpoint를 제공해야 한다.

```text
GET /health
GET /version
```

기존 프로젝트가 `/api/health`, `/api/version`을 사용한다면 경로를 설정값으로
지정해도 된다. 성공 조건은 다음과 같다.

- health endpoint는 준비 완료 후 2xx를 반환한다.
- version endpoint는 최소 아래 JSON을 반환한다.

```json
{
  "branch": "feature/foo",
  "commit": "40-character-full-sha",
  "short_commit": "12-char-sha",
  "built_at": "2026-09-05T00:00:00Z",
  "hostname": "feature-foo-alpha.mangagaki.net"
}
```

- `branch`와 `commit`은 배포기가 기대한 값과 정확히 같아야 한다.
- 이 메타데이터는 backend runtime과 frontend build 양쪽에 동일하게 주입해야 한다.

worker가 장시간 작업을 소유하는 프로젝트라면 일반 웹 재배포 시 worker를
보존해야 한다. worker 교체는 별도 opt-in 플래그로만 허용하고, 실행 중인 작업이
없음을 확인한 뒤 수행해야 한다.

## 6. 저장소 산출물

새 프로젝트에는 다음 파일과 책임을 구현한다. 파일명은 바꿀 수 있지만 책임을
합치거나 누락하면 안 된다.

```text
deploy.sh                 단일 브랜치/commit 배포의 유일한 진입점
auto-deploy.sh            원격 브랜치 polling, retry, stale 정리
undeploy.sh               route/DNS/container/worktree 제거, 데이터 보존
rollback.sh               기록된 직전 성공 commit 선택
verify-deployment.sh      상태를 바꾸지 않는 종합 검증
list-deployments.sh       로컬 상태와 컨테이너 현황 출력
cleanup-deployments.sh    프로젝트 소유 리소스만 보수적으로 정리
scripts/deploy-lib.sh     공통 검증, Cloudflare, Caddy, 상태 함수
compose.deploy.yaml       브랜치별 애플리케이션
compose.edge.yaml         공유 Caddy + cloudflared
deploy/Caddyfile          공유 edge 설정
deploy/Caddyfile.app      web 이미지의 정적 파일/API proxy 설정
deploy/*.Dockerfile       backend/frontend 등 이미지 정의
```

모든 shell entrypoint는 `set -Eeuo pipefail`을 사용해야 한다. 값 검색과 상태 변경은
공통 라이브러리를 거쳐야 하며, 수동 배포와 자동 배포가 서로 다른 파이프라인을
구현해서는 안 된다. 자동 배포와 롤백도 최종적으로 `deploy.sh <branch>
--revision <full-sha>`를 호출해야 한다.

## 7. 호스트 런타임 상태

상태는 Git에 커밋하지 않고 다음 구조로 저장한다.

```text
<state-root>/
├── units/<slug>/
│   ├── branch
│   ├── commit
│   ├── successful-commit
│   ├── previous-successful-commit
│   ├── hostname
│   ├── port
│   ├── project
│   ├── worktree
│   ├── built-at
│   ├── status
│   ├── tunnel-id
│   ├── dns-managed
│   └── data/
├── worktrees/<slug>/<short-sha>/
├── routes/<slug>.caddy
├── cloudflared/config.yml
├── locks/
└── logs/auto-deploy.log
```

작은 상태값은 임시 파일에 쓴 뒤 같은 filesystem 안에서 `mv`하여 원자적으로
교체해야 한다. branch별 작업에는 `locks/<slug>.lock`, watcher에는 별도 전역 lock,
포트 할당에는 별도 배타 lock을 사용해야 한다. 이미 실행 중인 유효 watcher가
있으면 다음 scan은 성공으로 건너뛴다. 비정상 종료로 남은 watcher lock은 기록된
PID가 존재하지 않을 때만 회수한다.

상태는 최소 `starting`, `running`, `failed`, `stopped`를 표현해야 한다. 실패한
배포는 어느 단계에서 실패했는지 로그와 선택적 GitHub 상태에 남겨야 한다.

## 8. 브랜치와 호스트명 규칙

### 8.1 slug 생성

1. 브랜치명을 소문자로 바꾼다.
2. `[a-z0-9-]` 이외의 연속 문자를 `-`로 바꾼다.
3. 연속 `-`를 하나로 합치고 앞뒤 `-`를 제거한다.
4. 기본 문자열을 최대 48자로 자른다.
5. 빈 문자열이면 배포를 거부한다.
6. 예약 이름 또는 다른 브랜치와 충돌하면 원래 브랜치명의 SHA-256 앞 8자를
   suffix로 붙인다.
7. 한 번 기록된 브랜치의 slug는 다음 배포에서도 재사용한다.

예:

```text
main        → alpha.mangagaki.net
impl        → impl-alpha.mangagaki.net
feature/foo → feature-foo-alpha.mangagaki.net
```

인증서 wildcard 제약을 단순하게 유지하기 위해 생성 주소는 항상 base domain 바로
아래의 한 label이어야 한다.

## 9. Cloudflare 1회 준비

### 9.1 zone과 인증서

- base domain은 Cloudflare에서 active zone이어야 한다.
- 브라우저 측 인증서는 Cloudflare Universal SSL을 사용한다.
- Cloudflare Origin Certificate와 private key를 저장소 밖에 발급·보관한다.
- Origin Certificate SAN은 프로덕션 호스트와 `*.{base-domain}`을 포함해야 한다.
- private key와 Tunnel credential JSON은 소유자만 읽도록 `chmod 600`을 권장한다.
- 배포기는 인증서/키의 존재, SAN, key pair 일치 여부를 배포 전에 검사해야 한다.
- 인증서, private key, Tunnel credential은 이미지에 COPY하거나 Git에 넣으면 안 된다.

### 9.2 Tunnel

프로젝트 전용 locally-managed Tunnel을 하나 만든다. 최초 생성은 운영자가
Cloudflare Dashboard 또는 `cloudflared tunnel create <name>`으로 수행한다. 생성된
Tunnel credential JSON은 저장소 밖에 둔다.

배포기가 생성하는 ingress는 다음 형태여야 한다.

```yaml
tunnel: <tunnel-uuid>
credentials-file: /etc/cloudflared/credentials.json

ingress:
  - hostname: alpha.mangagaki.net
    service: https://edge:443
    originRequest:
      noTLSVerify: true
  - hostname: "*.mangagaki.net"
    service: https://edge:443
    originRequest:
      noTLSVerify: true
  - service: http_status:404
```

두 번째 hostname은 실제 프로젝트의 base domain으로 치환한다. 이 wildcard ingress는
Tunnel 내부 라우팅 규칙일 뿐 DNS wildcard 레코드를 만들라는 뜻이 아니다.
배포기는 생성한 파일을 `cloudflared tunnel ingress validate`로 검사한 뒤 원자적으로
교체해야 한다.

`noTLSVerify`는 현재 JSB1과 동일한 호환성 선택이다. 통신 자체는 HTTPS이지만
cloudflared가 Caddy 인증서를 검증하지 않는다. 새 프로젝트에서 origin 검증까지
강제하려면 cloudflared trust 설정을 별도 설계·검증한 후 이 값을 제거한다.

### 9.3 DNS 권한과 레코드

권장 방식은 zone 범위를 대상 zone 하나로 제한한 Cloudflare API token이다.
필요 권한은 `Zone:Read`, `DNS:Edit`이다. token은 `CLOUDFLARE_API_TOKEN`으로
호스트의 보호된 환경 파일에만 둔다.

각 활성 호스트에 정확한 proxied CNAME을 만든다.

```text
<exact-hostname> CNAME <tunnel-uuid>.cfargotunnel.com, proxied=true
```

DNS wildcard 레코드는 만들지 않는다. 생성/갱신 규칙은 다음과 같다.

- 레코드가 없으면 생성한다.
- 동일 Tunnel을 가리키는 CNAME이면 그대로 재사용하고 proxy만 꺼져 있으면 켠다.
- 다른 대상이나 다른 type의 레코드가 있으면 기본적으로 실패한다.
- 명시적 overwrite 플래그가 있고 정확히 한 레코드만 있을 때만 교체한다.
- undeploy는 unit state에 `dns-managed=true`로 기록되어 있고, 동일 Tunnel target을
  가리키는 정확한 CNAME만 삭제한다.

Tunnel을 이름으로 찾는 구현은 cloudflared account credential(`cert.pem`)이 추가로
필요할 수 있다. 새 프로젝트는 가능하면 Tunnel UUID를 설정하여 이름 조회를
생략하고, DNS에는 최소 권한 API token을 사용하는 방식을 우선한다.

## 10. Caddy와 edge 계약

edge Compose project에는 `edge`와 `tunnel` 두 서비스만 둔다.

- `edge`: 고정 Caddy 설정과 생성 route 디렉터리, Origin Certificate/key를 read-only
  mount한다.
- `tunnel`: 생성 cloudflared 설정과 Tunnel credential JSON을 read-only mount한다.
- `tunnel`은 `edge` health가 성공한 뒤 시작한다.
- 두 서비스 모두 `restart: unless-stopped`와 로그 회전을 사용한다.
- edge의 host publish는 loopback에만 바인딩한다.
- route 디렉터리는 파일 하나가 아니라 디렉터리 전체를 mount한다. 원자적 파일
  교체 뒤 Docker Desktop이 오래된 inode를 보는 문제를 피하기 위함이다.

branch route는 다음 형식이다.

```caddyfile
https://feature-foo-alpha.mangagaki.net {
    import project_tls
    reverse_proxy host.docker.internal:9001
}
```

route 변경 절차는 반드시 다음 순서를 따른다.

1. 새 fragment를 임시 파일에 쓴다.
2. 원자적으로 목적 경로로 이동한다.
3. `caddy validate`를 실행한다.
4. 검증 성공 시 `caddy reload`를 실행한다.
5. 실패 시 직전 fragment를 복원하고 다시 reload한다.

## 11. 단일 배포 절차

`deploy.sh <branch> [--revision <commit-or-ref>]`는 아래 순서를 지켜야 한다.

1. 필수 명령(`docker`, `git`, `python3`, `curl`, `openssl`, `cloudflared`)을 검사한다.
2. domain, 포트 범위, credential, TLS 파일을 검증한다.
3. branch별 lock을 획득한다.
4. `origin/<branch>` 존재 여부를 `ls-remote`로 확인한다.
5. 해당 브랜치만 fetch하고 revision을 immutable full commit SHA로 해석한다.
6. unit state를 `starting`으로 기록한다.
7. 선택적 GitHub Commit Status `pending`과 Deployment `in_progress`를 기록한다.
8. `<state>/worktrees/<slug>/<short-sha>`에 detached worktree를 만든다.
9. branch의 기존 포트를 재사용하거나 전용 범위에서 loopback 빈 포트를 배정한다.
10. commit, UTC build time, branch, hostname을 build/runtime 변수로 export한다.
11. `docker compose config --quiet`으로 구성을 검사한다.
12. 새 이미지를 build한다. build 실패 시 기존 실행 컨테이너와 Caddy route는
    건드리지 않는다.
13. 애플리케이션 서비스를 recreate하고 Compose health를 기다린다.
14. loopback health endpoint가 2xx인지 확인한다.
15. loopback version endpoint의 branch/commit이 기대값과 일치하는지 확인한다.
16. 공유 edge/tunnel을 `up -d --wait`로 보장한다.
17. branch Caddy route를 기록하고 validate/reload한다.
18. loopback edge에서 HTTPS health와 제공 인증서 fingerprint를 확인한다.
19. 정확한 Cloudflare CNAME을 생성 또는 재사용한다.
20. 공개 `https://<hostname>/<health-path>`가 제한 시간 안에 2xx인지 확인한다.
21. 선택적 GitHub Deployment와 Commit Status를 `success`로 기록한다.
22. `successful-commit`, `previous-successful-commit`, `status=running`을 기록한다.
23. 현재와 직전 성공 worktree를 제외한 오래된 worktree를 정리한다.

어느 단계에서든 실패하면 `status=failed`, 실패 단계, 선택적 GitHub `failure`를
기록하고 non-zero로 종료해야 한다. 성공 상태는 20번까지 통과하기 전에 기록하면
안 된다.

기본 제한 시간 권장값:

| 항목 | 기본값 |
| --- | --- |
| 앱 준비 | 180초 |
| 공개 HTTPS 준비 | 180초 |
| health polling 간격 | 2초 |
| 개별 curl timeout | 3~10초 |

## 12. 자동 감시 절차

`auto-deploy.sh --install`은 현재 사용자의 crontab에 매분 실행 항목 하나를
설치한다. cron에는 secret 값을 넣지 않고 보호된 환경 파일 경로만 기록한다.

각 scan은 다음과 같이 동작한다.

1. 전역 watcher lock을 획득한다.
2. `git ls-remote --heads origin`을 한 번 호출해 전체 remote head snapshot을 만든다.
3. 각 브랜치의 remote SHA와 unit state의 commit을 비교한다.
4. SHA가 같고 상태가 정상이라면 no-op 처리한다.
5. 새 SHA면 `deploy.sh <branch> --revision <sha>`를 호출한다.
6. 실패한 동일 SHA는 기본 300초 뒤 재시도한다.
7. 실패 뒤 새 SHA가 나타나면 retry deadline과 관계없이 즉시 시도한다.
8. main은 `DEPLOY_AUTO_MAIN=true`가 아니면 배포하지 않고 skip 상태만 기록한다.
9. remote에서 사라진 preview branch에는 `stale-since`를 기록한다.
10. 기본 86,400초 동안 계속 사라져 있으면 정상 undeploy 절차를 호출한다.
11. 유예 시간 안에 branch가 복원되면 stale 상태를 지운다.
12. main은 자동 stale 제거하지 않는다.

지원 명령:

```sh
./auto-deploy.sh --once
./auto-deploy.sh --install
./auto-deploy.sh --status
./auto-deploy.sh --uninstall
```

`--uninstall`은 watcher만 제거하고 실행 중인 배포를 내리면 안 된다. macOS에서는
Docker Desktop 또는 OrbStack이 로그인 시 자동 시작되어야 한다.

## 13. 환경 설정과 secret

기본 보호 파일은 `~/.config/<project-key>/deploy.env`로 한다. 파일은 소유자만
읽을 수 있게 설정하고, loader는 owner 또는 group/world permission이 부적절하면
값을 출력하지 않고 경고해야 한다.

```sh
mkdir -p ~/.config/alpha
chmod 700 ~/.config/alpha

# 실제 값은 저장소 밖에서 작성한다.
chmod 600 ~/.config/alpha/deploy.env
```

필수 또는 조건부 변수:

| 변수 | 필수 | 설명 |
| --- | --- | --- |
| `DEPLOY_BASE_DOMAIN` | 예 | Cloudflare zone |
| `DEPLOY_MAIN_HOSTNAME` | 예 | 프로덕션 hostname |
| `DEPLOY_MAIN_BRANCH` | 예 | 기본 `main` |
| `DEPLOY_PORT_START/END` | 예 | 프로젝트 전용 범위 |
| `DEPLOY_TLS_CERT_PATH` | 예 | 저장소 밖 Origin Certificate |
| `DEPLOY_TLS_KEY_PATH` | 예 | 저장소 밖 private key |
| `DEPLOY_CLOUDFLARE_TUNNEL` | 예 | Tunnel UUID 권장, 이름 허용 |
| `DEPLOY_CLOUDFLARED_CREDENTIALS` | 예 | Tunnel credential JSON |
| `CLOUDFLARE_API_TOKEN` | 권장 | 대상 zone의 Zone:Read + DNS:Edit |
| `DEPLOY_CLOUDFLARED_ORIGIN_CERT` | 이름 조회 시 | cloudflared account credential |
| `DEPLOY_AUTO_MAIN` | 아니오 | 기본 `false` |
| `DEPLOY_RETRY_SEC` | 아니오 | 기본 `300` |
| `DEPLOY_STALE_BRANCH_GRACE_SEC` | 아니오 | 기본 `86400` |
| `DEPLOY_GITHUB_TOKEN` | 아니오 | GitHub 상태 보고 전용 |
| `DEPLOY_GITHUB_REPORTING_REQUIRED` | 아니오 | 기본 `false` |

이미 process environment에 설정된 값은 환경 파일보다 우선해야 한다. secret은
Compose environment, Docker build args, frontend bundle, version endpoint, unit state,
cron command, 로그에 전달하거나 기록해서는 안 된다.

## 14. undeploy와 rollback

### 14.1 undeploy

`undeploy.sh <branch>`는 다음 순서로 동작한다.

1. main이면 `--force` 없이는 거부한다.
2. branch lock을 획득한다.
3. Caddy fragment를 백업하고 제거한다.
4. Caddy validate/reload 실패 시 route를 복원하고 중단한다.
5. branch Compose project를 `down --remove-orphans`로 내린다.
6. 자신이 관리한다고 기록한 정확한 Cloudflare CNAME만 제거한다.
7. detached worktree와 port reservation을 제거한다.
8. unit state를 `stopped`로 기록한다.
9. GitHub environment가 있으면 `inactive`로 기록한다.

`units/<slug>/data`와 성공 commit 이력은 삭제하지 않는다. Docker volume을
자동 삭제하거나 `docker system prune`을 호출하면 안 된다.

### 14.2 rollback

rollback 대상은 다음 규칙으로만 고른다.

- 현재 시도가 실패했고 그 전 `successful-commit`이 있으면 그것을 사용한다.
- 그 외에는 `previous-successful-commit`을 사용한다.
- 확실한 기록이 없으면 추측하지 않고 실패한다.
- `HEAD^`를 자동 rollback 대상으로 사용하면 안 된다.

실행은 반드시 다음과 동등해야 한다.

```sh
./deploy.sh <branch> --revision <known-good-full-sha>
```

## 15. 검증과 운영 명령

최소 운영 인터페이스:

```sh
./deploy.sh <branch>
./deploy.sh <branch> --revision <full-sha>
./deploy.sh --status
./verify-deployment.sh [branch]
./rollback.sh <branch> --dry-run
./rollback.sh <branch>
./undeploy.sh <branch>
./cleanup-deployments.sh --dry-run
```

`verify-deployment.sh`는 읽기 전용이어야 하며 다음을 검사한다.

- Docker daemon 접근
- Origin Certificate/key 유효성
- edge와 cloudflared 실행 상태
- Caddy config validation
- branch backend/web health
- loopback health 2xx
- 생성 Caddy route의 hostname/port 일치
- 공개 HTTPS health 2xx
- 공개 version branch/commit과 unit state 일치

검사 실패 시 서비스를 시작하거나 재시작하지 않고 non-zero로 종료해야 한다.

cleanup은 프로젝트 label이 붙은 미사용 image와 명확히 소유한 unattached network만
대상으로 한다. 실행/중지 컨테이너가 참조하는 image와 모든 volume은 보호한다.
공용 BuildKit cache는 전용 builder임이 확인되지 않으면 삭제하지 않는다.

## 16. GitHub 보고(선택)

GitHub 통합은 배포 실행자가 아니라 관측 계층이다. fine-grained token은 대상
repository에 대해 다음 권한을 갖는다.

- Deployments: Read and write
- Commit statuses: Read and write
- Metadata: Read-only

commit SHA가 결정된 직후 build 전에 `pending`, 모든 공개 검증 성공 뒤 `success`,
실패 trap에서 `failure`를 기록한다. environment와 status context는 각각 아래처럼
결정적으로 생성한다.

```text
environment: <project-key>/<slug>
context:     <project-key>/deploy/<slug>
target URL:  https://<hostname>
```

기본 모드는 GitHub API 장애가 core 배포를 막지 않는 non-blocking이어야 한다.
운영 정책상 필수인 경우에만 `DEPLOY_GITHUB_REPORTING_REQUIRED=true`로 바꾼다.
undeploy는 environment만 inactive로 만들고 과거 commit status를 다시 쓰지 않는다.

## 17. 알려진 한계와 명시적 선택

- polling 주기가 1분이므로 즉시 배포는 보장하지 않는다.
- 모든 preview branch를 자동 배포하므로 untrusted contributor가 branch를 만들 수
  있는 저장소에는 그대로 적용하면 안 된다. allowlist 또는 승인 단계를 추가한다.
- 애플리케이션 Compose project를 같은 이름으로 recreate하므로 완전한 blue-green이
  아니다. build 실패는 기존 서비스를 보존하지만, startup/health 실패는 일부 새
  컨테이너를 남길 수 있다.
- DB migration이 하위 호환되지 않으면 commit rollback만으로 데이터 rollback이
  되지 않는다. migration 정책은 애플리케이션이 별도로 보장해야 한다.
- 한 hostname에 여러 DNS record가 있으면 자동 overwrite를 거부한다.
- wildcard Origin Certificate는 base domain 바로 아래 한 label만 포괄한다.
- locally-managed Tunnel credential JSON은 Tunnel 실행 권한을 가진 secret이다.

## 18. 인수 조건

아래 시나리오를 전부 통과하면 재현 완료로 본다.

1. 새 preview branch push 후 두 번의 polling 주기 안에 결정된 URL이 2xx를 반환한다.
2. 같은 SHA를 다시 scan해도 build/recreate하지 않는다.
3. 같은 branch의 새 SHA push 후 `/version`이 새 full SHA를 반환한다.
4. 두 preview branch가 동시에 실행되고 서로 다른 loopback port와 데이터를 쓴다.
5. branch명 `feature/foo`가 DNS-safe hostname으로 변환된다.
6. 정규화 충돌하는 두 branch가 서로 다른 안정적인 slug를 얻는다.
7. build 실패 시 기존 공개 route가 변경되지 않고 unit이 `failed`가 된다.
8. 잘못된 Caddy fragment 적용 시 이전 route가 복원된다.
9. 다른 대상을 가리키는 기존 DNS record를 기본 설정으로 덮어쓰지 않는다.
10. 공개 health 성공 전에는 배포가 `running` 또는 GitHub `success`가 되지 않는다.
11. remote branch 삭제 직후에는 유지되고, 유예 시간 뒤 route/DNS/container만
    제거되며 branch 데이터는 남는다.
12. main undeploy가 `--force` 없이 거부된다.
13. rollback dry-run이 실제 변경 없이 정확한 기록 대상만 출력한다.
14. secret 문자열이 `docker inspect`, image history, crontab, unit state, frontend,
    `/version`, 일반 로그에 나타나지 않는다.
15. 호스트 재부팅 뒤 Docker 자동 시작만으로 edge, tunnel, running branch 컨테이너가
    복구되고 읽기 전용 검증을 통과한다.

## 19. 구현 순서

1. 프로젝트 키, hostname 규칙, 전용 포트 범위를 확정한다.
2. 애플리케이션 Docker build와 health/version 계약을 먼저 완성한다.
3. branch별 Compose와 loopback publish를 구현한다.
4. 상태 저장, slug, lock, port allocator를 구현하고 단위 테스트한다.
5. 수동 `deploy.sh`를 Cloudflare 없이 loopback까지 검증한다.
6. 프로젝트 전용 Origin Certificate와 Tunnel을 준비한다.
7. Caddy edge, Tunnel ingress, 정확한 DNS record 관리를 연결한다.
8. 공개 HTTPS 검증과 실패 복구를 구현한다.
9. undeploy, rollback, verify, conservative cleanup을 구현한다.
10. 마지막에 watcher를 설치하고 main 자동 배포 여부를 명시적으로 결정한다.
11. 선택적으로 GitHub 보고를 연결한다.

## 20. JSB1 참조 구현과의 대응

| 이 명세의 책임 | JSB1 참조 파일 |
| --- | --- |
| 배포 orchestration | `deploy.sh` |
| 공통 상태/Cloudflare/Caddy 함수 | `scripts/deploy-lib.sh` |
| remote polling과 stale 제거 | `auto-deploy.sh` |
| branch 애플리케이션 | `compose.deploy.yaml` |
| 공유 edge와 tunnel | `compose.edge.yaml` |
| edge TLS/route import | `deploy/Caddyfile` |
| 제거/롤백/검증 | `undeploy.sh`, `rollback.sh`, `verify-deployment.sh` |
| 운영 동작 설명 | `README.md`의 Branch Preview Deployments |

2026-09-05 기준 JSB1 호스트에서는 per-user cron watcher가 매분 설치되어 있고,
preview branch 자동 배포가 동작하며, main 자동 배포는 꺼져 있다. 새 프로젝트도
처음에는 main 자동 배포를 끈 상태로 인수 시험을 완료한 뒤 별도로 opt-in한다.

## 21. 참고 자료

- Cloudflare Docs, Create a locally-managed tunnel:
  <https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/get-started/create-local-tunnel/>
- Cloudflare Docs, Locally-managed tunnel configuration file:
  <https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/configure-tunnels/local-management/configuration-file/>
- Cloudflare Docs, API token creation:
  <https://developers.cloudflare.com/fundamentals/api/get-started/create-token/>

