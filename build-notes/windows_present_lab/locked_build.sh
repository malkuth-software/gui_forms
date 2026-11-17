#!/bin/sh
rc=0
/d/malkuth/projects/tools/with_build_lock.sh "windows-present-surfaces:claude/windows-present-surfaces" /d/gw_present_lab/build /d/gw_present_lab/build.sh > /d/gw_present_lab/build.log 2>&1 || rc=$?
echo "rc=$rc" >> /d/gw_present_lab/build.log
exit $rc
