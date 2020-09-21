#!/bin/tclsh

load tclrpc.so
load tclrega.so

if { $argc < 1 } {
  exit 1
}

set state [lindex $argv 0]

set script "
  object alObj = null;
  string sSysVarId;
  foreach(sSysVarId, dom.GetObject(ID_SYSTEM_VARIABLES).EnumUsedIDs()) {
    object oSysVar = dom.GetObject(sSysVarId);
    if((alObj == null) && (oSysVar.Name() == \"HB-RF-ETH Connection\")) {
      alObj=oSysVar;
    }
  }

  if(alObj == null) {
    alObj = dom.CreateObject(OT_ALARMDP);
    if(alObj != null) {
      alObj.Name(\"HB-RF-ETH Connection\");
      alObj.ValueType(ivtBinary);
      alObj.ValueSubType(istAlarm);
      alObj.ValueName0(\"\${hb_eth_connection_connected}\");
      alObj.ValueName1(\"\${hb_eth_connection_disconnected}\");
      alObj.ValueUnit(\"\");
      alObj.AlType(atSystem);
      alObj.AlArm(true);
      alObj.AlSetBinaryCondition();
      alObj.State(false);
      dom.GetObject(ID_SYSTEM_VARIABLES).Add(alObj.ID());
      dom.RTUpdate(1);
    }
  }

  if(alObj != null) {
    alObj.ValueType(ivtBinary);
    alObj.ValueSubType(istAlarm);
    alObj.DPInfo(\"\${hb_eth_connection_description}\");
    alObj.State($state);
  }
"

rega_script $script

