#!/bin/tclsh

load tclrpc.so
load tclrega.so

set script "
boolean are_sv_created = false;
string sv_id;

foreach (sv_id, dom.GetObject(ID_SYSTEM_VARIABLES).EnumUsedIDs()) {
  if ((sv_id == 40) || (sv_id == 41) || (sv_id == ID_PRESENT)) {
    are_sv_created = true;
  }
}
"

for {set i 0} {$i < 60} {incr i} {
  array set result [rega_script $script]
  set ise_are_sv_created $result(are_sv_created)

  if {$ise_are_sv_created} {
    break
  }

  exec sleep 5
}

