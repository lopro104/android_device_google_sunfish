---
name: Report a issue
about: For reporting issues
title: ''
labels: ''
assignees: ''

---

**Describe the bug**
A clear and concise description of what the bug is.
Example: "Feature" doesn't work when I try to use it.

**To Reproduce**
Steps to reproduce the behavior:
1. Go to '...'
2. Click on '....'
3. Scroll down to '....'
4. See error

**Expected behavior**
What should've happened?

**Screenshots**
If applicable, add screenshots to help explain your problem.

**getting logs**
- if you have access to your computer, replicate the issue while running the `adb logcat` command like this:
- `adb logcat > pixel4a.log`

- If you have root access also run `adb shell su -c "dmesg" > pixel4admesg.log`

- after that you should see pixel4a.log and/or pixel4admesg.log wherever you ran your terminal, upload those files in this issue.

**OS info:**
- OS: [Android 16/17]
- Version: [Any issues with an older version will be ignored, always be on the latest android version to report a bug]

**Additional context**
Add any other context about the problem here.
