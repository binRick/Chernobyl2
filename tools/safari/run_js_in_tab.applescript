on run argv
  set js to (read POSIX file (item 2 of argv) as «class utf8»)
  tell application "Safari"
    repeat with w in windows
      repeat with t in tabs of w
        if (URL of t) contains (item 1 of argv) then return (do JavaScript js in t)
      end repeat
    end repeat
  end tell
  return "no tab"
end run
