#!/bin/bash
# ./main u 12000 u.conf
# ./main v 12001 v.conf
# ./main w 12002 w.conf
# ./main x 12003 x.conf
# ./main y 12004 y.conf
# ./main z 12005 z.conf
wt -d . "./main" u 12000 u.conf \; -d . ./main v 12001 v.conf \; -d . ./main w 12002 w.conf \; -d . ./main x 12003 x.conf \; -d . ./main y 12004 y.conf \; -d . ./main z 12005 z.conf
