# syntax=docker/dockerfile:1

FROM node:24-bookworm-slim AS builder

USER root

RUN apt-get update \
 && apt-get install -y --no-install-recommends -o APT::Keep-Downloaded-Packages=false \
    build-essential \
    ca-certificates \
    curl \
    git \
    jq \
    pkg-config \
    python-is-python3 \
    python3 \
    quilt \
    rsync \
    wget \
    libffi-dev \
    libkrb5-dev \
    libsecret-1-dev \
    libssl-dev \
    libx11-dev \
    libxkbfile-dev \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/* /tmp/*

WORKDIR /opt/code-server

ARG CODE_SERVER_REF=main
ARG VSCODE_COMMIT=a22d00300655c17490ce63dffc28bcdcedcd82c4

COPY code-server/ ./

RUN set -eux \
    && find . -type f -empty ! -path './lib/vscode/*' | while read -r f; do \
        rel="${f#./}"; \
        echo "repairing empty file: ${rel}"; \
        curl -fsSL "https://raw.githubusercontent.com/coder/code-server/${CODE_SERVER_REF}/${rel}" -o "$f" \
          || rm -f "$f"; \
      done

RUN rm -rf lib/vscode \
    && mkdir -p lib/vscode \
    && curl -fsSL "https://codeload.github.com/microsoft/vscode/tar.gz/${VSCODE_COMMIT}" \
    | tar -xz --strip-components=1 -C lib/vscode

RUN curl -fsSL "https://raw.githubusercontent.com/coder/code-server/${CODE_SERVER_REF}/patches/series" \
        -o patches/series \
    && while read -r patch; do \
        curl -fsSL "https://raw.githubusercontent.com/coder/code-server/${CODE_SERVER_REF}/patches/${patch}" \
            -o "patches/${patch}"; \
    done < patches/series

ENV PYTHON=/usr/bin/python3 \
    ELECTRON_SKIP_BINARY_DOWNLOAD=1 \
    PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1 \
    PUPPETEER_SKIP_DOWNLOAD=1 \
    DISABLE_V8_COMPILE_CACHE=1 \
    SKIP_COPILOT=1 \
    MINIFY=false \
    GOMEMLIMIT=1600MiB \
    NODE_OPTIONS=--max-old-space-size=2048 \
    HOME=/home/coder \
    OS=linux \
    SHELL=/bin/bash \
    VERSION=0.0.0 \
    VSCODE_COMMIT=${VSCODE_COMMIT} \
    BUILD_SOURCEVERSION=${VSCODE_COMMIT}

# Single build step: install, compile, then delete sources and caches so this
# layer only retains runtime artifacts (not multi-GB node_modules trees).
RUN ln -s . code-server \
    && quilt push -a \
    && rm code-server \
    && set -eux \
    && DIRS=lib/vscode/build/npm/dirs.ts \
    && GULP=lib/vscode/build/gulpfile.extensions.ts \
    && EXT_EXCLUDE=lib/vscode/build/lib/extensions.ts \
    && for path in \
        test/automation \
        test/integration/browser \
        test/monaco \
        test/smoke \
        test/mcp \
        extensions/copilot \
        extensions/css-language-features \
        extensions/debug-server-ready \
        extensions/git \
        extensions/github \
        extensions/github-authentication \
        extensions/html-language-features \
        extensions/ipynb \
        extensions/json-language-features \
        extensions/markdown-language-features \
        extensions/markdown-math \
        extensions/media-preview \
        extensions/merge-conflict \
        extensions/mermaid-markdown-features \
        extensions/microsoft-authentication \
        extensions/notebook-renderers \
        extensions/php-language-features \
        extensions/simple-browser \
        extensions/terminal-suggest \
        extensions/typescript-language-features \
        extensions/vscode-api-tests \
        extensions/vscode-colorize-tests \
        extensions/vscode-colorize-perf-tests \
        extensions/vscode-test-resolver \
        .vscode/extensions/vscode-selfhost-test-provider \
        .vscode/extensions/vscode-extras \
        .vscode/extensions/vscode-pr-pinger \
      ; do \
        sed -i "\|${path}|d" "$DIRS" "$GULP" "$EXT_EXCLUDE"; \
        case "$path" in \
          extensions/*) \
            ext_name="${path#extensions/}"; \
            sed -i "\|${ext_name}/|d" "$EXT_EXCLUDE"; \
            sed -i "/const excludedExtensions = \[/,/\];/ s/];/\t'${ext_name}',\n];/" "$EXT_EXCLUDE"; \
            ;; \
        esac; \
        rm -rf "lib/vscode/${path}"; \
      done \
    && find lib/vscode/extensions -name tsconfig.json -size 0 | while read -r tsconfig; do \
        ext_dir=$(dirname "$tsconfig"); \
        rel="${ext_dir#lib/vscode/}"; \
        name=$(basename "$ext_dir"); \
        echo "excluding broken extension: ${rel}"; \
        sed -i "\|${rel}|d" "$DIRS" "$GULP" "$EXT_EXCLUDE"; \
        sed -i "\|${name}/|d" "$EXT_EXCLUDE"; \
        sed -i "/const excludedExtensions = \[/,/\];/ s/];/\t'${name}',\n];/" "$EXT_EXCLUDE"; \
        rm -rf "$ext_dir"; \
      done \
    && test -s tsconfig.json \
    && jq 'del(.scripts["test-browser"], .scripts["playwright-install"], .devDependencies["@playwright/cli"], .devDependencies["@playwright/test"], .devDependencies["playwright-core"])' lib/vscode/package.json > lib/vscode/package.json.tmp \
    && mv lib/vscode/package.json.tmp lib/vscode/package.json \
    && git -C lib/vscode init \
    && sed -i '/tsgo-typecheck/d' lib/vscode/build/gulpfile.vscode.ts \
    && sed -i '/Type-check with tsgo/d' lib/vscode/build/gulpfile.vscode.ts \
    && sed -i '/\.\.\.getBuildRootsForExtension(extensionPath)\.map(root => typeCheckExtensionStream(root, forWeb)),/d' lib/vscode/build/lib/extensions.ts \
    && sed -i "s/return spawnTsgo(tsconfigPath, { taskName: 'typechecking extension (tsgo)', noEmit: true });/return Promise.resolve();/" lib/vscode/build/lib/extensions.ts \
    && sed -i "s/return createTsgoStream(tsconfigPath, { taskName: 'typechecking extension (tsgo)', noEmit: true });/return es.through();/" lib/vscode/build/lib/extensions.ts \
    && sed -i 's/return Promise.all(\[esbuildTask, \.\.\.typeCheckTasks\]).then(() => undefined);/return esbuildTask.then(() => undefined);/' lib/vscode/build/lib/extensions.ts \
    && sed -i '/esbuild-vscode-min/d' lib/vscode/build/gulpfile.vscode.ts \
    && sed -i '/esbuild-vscode-reh-min/d' lib/vscode/build/gulpfile.vscode.ts \
    && sed -i "s/runEsbuildBundle('out-vscode-reh-web-min', true, true, 'server-web'/runEsbuildBundle('out-vscode-reh-web', false, true, 'server-web'/" lib/vscode/build/gulpfile.vscode.ts \
    && sed -i 's/esbuild-vscode-reh-web-min/esbuild-vscode-reh-web/' lib/vscode/build/gulpfile.vscode.ts \
    && sed -i '/prepareCopilotRipgrepShimTaskREH(platform, arch, destinationFolderName)/d' lib/vscode/build/gulpfile.reh.ts \
    && SKIP_SUBMODULE_DEPS=1 npm install \
    && npm ci --prefix lib/vscode \
    && npm run build:vscode \
    && bash -c 'source ./ci/lib.sh && rsync -a "lib/vscode-reh-web-${VSCODE_TARGET}/" lib/vscode/' \
    && npm run build \
    && rm -rf \
        lib/vscode/src \
        lib/vscode/build \
        lib/vscode/test \
        lib/vscode/.build \
        lib/vscode/.github \
        lib/vscode/.git \
        lib/vscode/out-build \
        lib/vscode/out-vscode-min \
        lib/vscode/out-vscode-reh-min \
        lib/vscode/out-vscode-reh-web \
        lib/vscode-reh-web-* \
        patches \
        ci/dev \
        test \
    && npm prune --omit=dev \
    && npm cache clean --force \
    && rm -rf /root/.npm /root/.cache /tmp/*

FROM node:24-bookworm-slim

USER root

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    clang-format \
    gdb \
    valgrind \
    make \
    python3 \
    vim \
 && apt-get clean \
 && rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/* /tmp/*

RUN if id node >/dev/null 2>&1; then userdel -r node; fi \
    && if getent group node >/dev/null 2>&1; then groupdel node; fi \
    && useradd --create-home --home-dir /home/coder --uid 1000 --shell /bin/bash coder

COPY --from=builder /opt/code-server /opt/code-server

ENV HOME=/home/coder \
    OS=linux \
    SHELL=/bin/bash \
    VERSION=0.0.0 \
    PYTHON=/usr/bin/python3

WORKDIR /home/coder

EXPOSE 8080

USER coder

CMD ["node", "/opt/code-server/out/node/entry.js", "--bind-addr", "0.0.0.0:8080"]
