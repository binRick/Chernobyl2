#!/bin/bash
# Cut the demo: ./edit.sh  ->  build/demo/chernobyl2-demo.mp4 (+ -sheet.jpg)
# Intro card (4.5 s) -> the segments in cut.txt -> end card (6 s).
# cut.txt: take | in(s) | dur(s) | vo beat or - | vo start relative to the segment (s; negative reaches back,
#          e.g. into the intro card). Takes from shoot.sh (build/demo/takes), narration from build/demo/vo/<n>-<beat>.mp3.
# The narration track is built by concatenation (silence, line, silence, line ...), never amix: amix of many delayed
# inputs gave every line the same length in ffmpeg 8.1 and cut the long ones short. Lines may not overlap.
# Game sound ducks under the voice; loudness -14 LUFS (YouTube). check-vo.sh proves each line plays whole, on time.
cd "$(dirname "$0")/../.." || exit 1
T=build/demo/takes; V=build/demo/vo; W=${W:-build/demo/edit}; OUT=${OUT:-build/demo/chernobyl2-demo.mp4}
INTRO=4.5; END=6
rm -rf $W; mkdir -p $W
dur(){ ffprobe -v error -show_entries format=duration -of csv=p=0 "$1"; }
vtrim(){ mkdir -p $W/vo; local o=$W/vo/$(basename "${1%.*}").wav; python3 tools/demo/vo-trim.py trim "$1" "$o" && echo "$o"; }   # cut ElevenLabs' stray tail

# a card: black, three layers fading in at their times (and the intro fading out at its end), silent
card(){  # card <name> <prefix> <len> <fadeout 0|1> <t0> <t1> <t2>
  local n=$1 p=$2 len=$3 fo=$4 a=$5 b=$6 c=$7 out=""
  [ "$fo" = 1 ] && out=",fade=t=out:st=$(awk -v l=$len 'BEGIN{print l-0.9}'):d=0.7:alpha=1"
  ffmpeg -nostdin -v error -y -f lavfi -i color=black:s=1920x1080:r=60:d=$len \
    -loop 1 -t $len -i $W/${p}0.png -loop 1 -t $len -i $W/${p}1.png -loop 1 -t $len -i $W/${p}2.png \
    -f lavfi -t $len -i anullsrc=r=48000:cl=stereo \
    -filter_complex "[1]format=rgba,fade=t=in:st=$a:d=0.8:alpha=1$out[a];[2]format=rgba,fade=t=in:st=$b:d=0.8:alpha=1$out[b];[3]format=rgba,fade=t=in:st=$c:d=0.8:alpha=1$out[c];[0][a]overlay[v1];[v1][b]overlay[v2];[v2][c]overlay,format=yuv420p,fps=60[v]" \
    -map "[v]" -map 4:a -t $len -c:v libx264 -crf 14 -preset fast -c:a pcm_s16le $W/$n.mov
}
python3 tools/demo/title.py $W intro && python3 tools/demo/title.py $W end || exit 1
card intro intro $INTRO 1 0.2 1.0 1.7 || exit 1
echo "file 'intro.mov'" > $W/list.txt

t=$INTRO; n=0; : > $W/vo.txt
while IFS='|' read -r -u 3 take in len vo at; do
  take=$(echo $take); [ -z "$take" ] && continue
  in=$(echo $in); len=$(echo $len); vo=$(echo $vo); at=$(echo ${at:-0})
  [ -s $T/$take.mp4 ] || { echo "missing take $take" >&2; exit 1; }
  s=$W/seg$(printf %02d $n).mov
  ffmpeg -nostdin -v error -y -ss $in -t $len -i $T/$take.mp4 \
    -vf "fps=60,scale=1920:1080$([ $n = 0 ] && echo ,fade=t=in:st=0:d=0.8),format=yuv420p" -af "aresample=48000,aformat=channel_layouts=stereo,apad,atrim=0:$len" \
    -c:v libx264 -crf 14 -preset fast -c:a pcm_s16le $s || exit 1
  echo "file '$(basename $s)'" >> $W/list.txt
  if [ "$vo" != "-" ] && [ -n "$vo" ]; then
    clip=$(ls $V/*-$vo.mp3 2>/dev/null | head -1); [ -n "$clip" ] || { echo "no vo for $vo" >&2; exit 1; }
    clip=$(vtrim $clip) || exit 1
    echo "$clip $(awk -v a=$t -v b=$at 'BEGIN{printf "%.3f", a+b}')" >> $W/vo.txt
  fi
  t=$(awk -v a=$t -v b=$len 'BEGIN{print a+b}'); n=$((n+1))
done 3< <(grep -v '^#' tools/demo/cut.txt)

card title title $END 0 0 0.6 1.6 || exit 1
echo "file 'title.mov'" >> $W/list.txt
tl=$(ls $V/*-title.mp3 2>/dev/null | head -1); [ -n "$tl" ] && tl=$(vtrim $tl) && echo "$tl $(awk -v a=$t 'BEGIN{printf "%.3f",a+1.2}')" >> $W/vo.txt
total=$(awk -v a=$t -v e=$END 'BEGIN{print a+e}')
ffmpeg -nostdin -v error -y -f concat -safe 0 -i $W/list.txt -c copy $W/body.mov || exit 1

# the narration track, by concatenation: silence up to each line, the line, ... then silence to the end
args=(); fl=""; parts=""; k=0; prev=0; np=0
while read -r clip at; do
  d=$(dur "$clip"); gap=$(awk -v a=$at -v p=$prev 'BEGIN{print a-p}')
  awk -v g=$gap 'BEGIN{exit !(g<-0.001)}' && { echo "narration overlaps before $clip (gap $gap s)" >&2; exit 1; }
  awk -v g=$gap 'BEGIN{exit !(g>0.001)}' && { fl+="aevalsrc=0|0:c=stereo:s=48000:d=$gap[s$np];"; parts+="[s$np]"; np=$((np+1)); }
  args+=(-i "$clip"); fl+="[$k:a]aresample=48000,aformat=sample_fmts=fltp:channel_layouts=stereo,volume=1.6[c$k];"; parts+="[c$k]"; np=$((np+1)); k=$((k+1))
  prev=$(awk -v a=$at -v d=$d 'BEGIN{print a+d}')
done < $W/vo.txt
tail=$(awk -v a=$total -v p=$prev 'BEGIN{print a-p}'); fl+="aevalsrc=0|0:c=stereo:s=48000:d=$tail[s$np];"; parts+="[s$np]"; np=$((np+1))
fl+="${parts}concat=n=$np:v=0:a=1,aformat=sample_fmts=fltp:channel_layouts=stereo[o]"
ffmpeg -nostdin -v error -y "${args[@]}" -filter_complex "$fl" -map "[o]" -c:a pcm_f32le $W/vo.wav || exit 1

# the mix: game ducked under the voice, then loudness
gv=${GAME_VOL:-0.9}
fl="[1:a]asplit[vo1][vo2];[0:a]volume=$gv[g];[g][vo1]sidechaincompress=threshold=0.03:ratio=6:attack=40:release=400[gd];"
fl+="[gd][vo2]amix=inputs=2:normalize=0:duration=first,atrim=0:$total,loudnorm=I=-14:TP=-1.5:LRA=11,aresample=48000,alimiter=limit=0.74:attack=2:release=60:level=false[a];"
fl+="[0:v]null[v]"                      # (the gameplay fades in on its own first segment; a fade here would black out the intro)
ffmpeg -nostdin -v error -y -i $W/body.mov -i $W/vo.wav -filter_complex "$fl" -map "[v]" -map "[a]" \
  -c:v libx264 -crf 17 -preset slow -pix_fmt yuv420p -r 60 -c:a aac -b:a 192k -ar 48000 -movflags +faststart $OUT || exit 1
d=$(dur $OUT)
ffmpeg -nostdin -v error -y -i $OUT -vf "fps=$(awk -v d=$d 'BEGIN{print 48/d}'),scale=320:-1,tile=8x6:padding=2" -frames:v 1 ${OUT%.mp4}-sheet.jpg
echo "$OUT  ${d}s"
