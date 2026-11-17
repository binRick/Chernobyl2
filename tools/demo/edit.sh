#!/bin/bash
# Cut the demo: ./edit.sh  ->  build/demo/chernobyl2-demo.mp4 (+ -sheet.jpg)
# cut.txt: take | in(s) | dur(s) | vo beat or - | vo start within the segment (s)
# Takes from shoot.sh (build/demo/takes), narration clips from build/demo/vo/<n>-<beat>.mp3.
# Game sound ducks under the voice; ends on a black title card; loudness -14 LUFS (YouTube).
cd "$(dirname "$0")/../.." || exit 1
T=build/demo/takes; V=build/demo/vo; W=build/demo/edit; OUT=build/demo/chernobyl2-demo.mp4
rm -rf $W; mkdir -p $W
t=0; n=0; : > $W/list.txt; : > $W/vo.txt
while IFS='|' read -r -u 3 take in dur vo at; do
  take=$(echo $take); [ -z "$take" ] && continue
  in=$(echo $in); dur=$(echo $dur); vo=$(echo $vo); at=$(echo ${at:-0})
  [ -s $T/$take.mp4 ] || { echo "missing take $take" >&2; exit 1; }
  s=$W/seg$(printf %02d $n).mov
  ffmpeg -nostdin -v error -y -ss $in -t $dur -i $T/$take.mp4 \
    -vf "fps=60,scale=1920:1080,format=yuv420p" -af "aresample=48000,aformat=channel_layouts=stereo" \
    -c:v libx264 -crf 14 -preset fast -c:a pcm_s16le $s || exit 1
  echo "file '$(basename $s)'" >> $W/list.txt
  if [ "$vo" != "-" ] && [ -n "$vo" ]; then
    clip=$(ls $V/*-$vo.mp3 2>/dev/null | head -1); [ -n "$clip" ] || { echo "no vo for $vo" >&2; exit 1; }
    echo "$clip $(awk -v a=$t -v b=$at 'BEGIN{printf "%d", (a+b)*1000}')" >> $W/vo.txt
  fi
  t=$(awk -v a=$t -v b=$dur 'BEGIN{print a+b}'); n=$((n+1))
done 3< <(grep -v '^#' tools/demo/cut.txt)

# the title card: 6 s of black, the name in the game's red, then the place to play it (layers from title.py)
python3 tools/demo/title.py $W || exit 1
ffmpeg -nostdin -v error -y -f lavfi -i color=black:s=1920x1080:r=60:d=6 -loop 1 -t 6 -i $W/title0.png -loop 1 -t 6 -i $W/title1.png -loop 1 -t 6 -i $W/title2.png \
  -f lavfi -t 6 -i anullsrc=r=48000:cl=stereo \
  -filter_complex "[1]format=rgba,fade=t=in:st=0:d=0.8:alpha=1[a];[2]format=rgba,fade=t=in:st=0.6:d=0.8:alpha=1[b];[3]format=rgba,fade=t=in:st=1.6:d=0.8:alpha=1[c];[0][a]overlay[v1];[v1][b]overlay[v2];[v2][c]overlay,format=yuv420p,fps=60[v]" \
  -map "[v]" -map 4:a -t 6 -c:v libx264 -crf 14 -preset fast -c:a pcm_s16le $W/title.mov || exit 1
echo "file 'title.mov'" >> $W/list.txt
# (the title line plays over the card)
tl=$(ls $V/*-title.mp3 2>/dev/null | head -1); [ -n "$tl" ] && echo "$tl $(awk -v a=$t 'BEGIN{printf "%d",(a+1.2)*1000}')" >> $W/vo.txt
total=$(awk -v a=$t 'BEGIN{print a+6}')

ffmpeg -nostdin -v error -y -f concat -safe 0 -i $W/list.txt -c copy $W/body.mov || exit 1

# narration track: every clip at its time, then the game ducks under it
args=(); fl=""; k=1
while read -r clip ms; do args+=(-i "$clip"); fl+="[$k:a]aresample=48000,aformat=channel_layouts=stereo,adelay=${ms}|${ms},volume=1.6[v$k];"; k=$((k+1)); done < $W/vo.txt
m=""; for ((j=1;j<k;j++)); do m+="[v$j]"; done
fl+="${m}amix=inputs=$((k-1)):normalize=0,apad[vo];[vo]asplit[vo1][vo2];"
fl+="[0:a]volume=0.9[g];[g][vo1]sidechaincompress=threshold=0.03:ratio=6:attack=40:release=400[gd];"
fl+="[gd][vo2]amix=inputs=2:normalize=0,atrim=0:$total,loudnorm=I=-14:TP=-1.5:LRA=11,aresample=48000,alimiter=limit=0.74:attack=2:release=60:level=false[a];"
fl+="[0:v]fade=t=in:st=0:d=0.8[v]"
ffmpeg -nostdin -v error -y -i $W/body.mov "${args[@]}" -filter_complex "$fl" -map "[v]" -map "[a]" \
  -c:v libx264 -crf 17 -preset slow -pix_fmt yuv420p -r 60 -c:a aac -b:a 192k -ar 48000 -movflags +faststart $OUT || exit 1
d=$(ffprobe -v error -show_entries format=duration -of csv=p=0 $OUT)
ffmpeg -nostdin -v error -y -i $OUT -vf "fps=$(awk -v d=$d 'BEGIN{print 48/d}'),scale=320:-1,tile=8x6:padding=2" -frames:v 1 ${OUT%.mp4}-sheet.jpg
echo "$OUT  ${d}s"
