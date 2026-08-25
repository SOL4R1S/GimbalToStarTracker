#!/usr/bin/env python3
"""web/index.html → include/webui.h 변환기 (단일 소스 유지용)"""
import sys, pathlib

def main() -> None:
    if len(sys.argv) != 3:
        sys.exit("usage: gen_webui.py <index.html> <webui.h>")
    html = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    assert "]HTML]" not in html, "HTML 본문에 종결자 ]HTML] 포함 불가"
    out = (
        "#pragma once\n// 자동 생성 파일 — 수정 금지. 원본: web/index.html\n"
        "// 재생성: python3 tools/gen_webui.py web/index.html include/webui.h\n"
        'static const char INDEX_HTML[] PROGMEM = R"HTML(' + html + ')HTML";\n'
    )
    pathlib.Path(sys.argv[2]).write_text(out, encoding="utf-8")
    print(f"wrote {sys.argv[2]} ({len(html)} bytes)")

if __name__ == "__main__":
    main()
