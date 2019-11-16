# Cyarr - Yet Another RTMP Relayer

PeerCast Gateway で使うために作ったサーバープログラムで、その機能は
RTMP をサポートするメディアサーバーから、ffmpeg を使用して他のメディア
サーバーにライブ ストリームを転送することです。APIは JSON-RPC によって
呼び出されます。

## API

* start(src, dst) → pid
* kill(pid) → true|false
* stats → \[{pid, src, dst, created\_at}\]
