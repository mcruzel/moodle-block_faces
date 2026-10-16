#!/bin/bash
# shot.sh fichier.svg sortie.png largeur hauteur
SVG=$(readlink -f "$1"); OUT=$(readlink -f "$2"); W=$3; H=$4
TMP=$(mktemp -d)
cat > $TMP/page.html <<HTML
<!doctype html><html><head><style>html,body{margin:0;padding:0;background:#fff;overflow:hidden}img{display:block;width:${W}px;height:${H}px}</style></head>
<body><img src="file://$SVG"></body></html>
HTML
timeout 300 /opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell --no-sandbox --disable-gpu --hide-scrollbars \
  --allow-file-access-from-files --window-size=$W,$H --virtual-time-budget=60000 --screenshot=$OUT file://$TMP/page.html 2>/dev/null
rm -rf $TMP
