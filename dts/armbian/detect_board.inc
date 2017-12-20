if [ ! -z "$1" ]; then
  DEV_TREE_COMPATIBLE="$1"
elif [ -e /proc/device-tree/compatible ]; then
  DEV_TREE_COMPATIBLE=$(strings /proc/device-tree/compatible)
fi

if [ ! -z "$DEV_TREE_COMPATIBLE" ]; then
  for str in $DEV_TREE_COMPATIBLE; do
    case $str in
      xunlong,orangepi-one|xunlong,orangepi-lite|xunlong,orangepi-plus|xunlong,orangepi-plus2e|xunlong,orangepi-2|xunlong,orangepi-pc|xunlong,orangepi-pc-plus)
        OVERLAY_MODE='overlay'
        OVERLAY_FILE='pivccu-orangepi-h3.dtbo'
        break
        ;;
      xunlong,orangepi-zero|xunlong,orangepi-r1)
        OVERLAY_MODE='overlay'
        OVERLAY_FILE='pivccu-orangepi-h2plus.dtbo'
        break
        ;;
      lemaker,bananapi)
        OVERLAY_MODE='overlay'
        OVERLAY_FILE='pivccu-bananapi-m1.dtbo'
	break;
	;;
      lemaker,bananapro)
        OVERLAY_MODE='overlay'
        OVERLAY_FILE='pivccu-bananapi-pro.dtbo'
	break;
        ;;
      asus,rk3288-tinker|asus,rk3288-tinker-s)
        OVERLAY_MODE='patch'
        INCLUDE_FILE='/var/lib/piVCCU/dts/tinkerboard.dts.include'
        FDT_FILE=rk3288-tinker.dtb
        break
        ;;
      pine64,rock64)
        OVERLAY_MODE='patch'
        INCLUDE_FILE='/var/lib/piVCCU/dts/rock64.dts.include'
        FDT_FILE=rockchip/rk3328-rock64.dtb
        break
        ;;
      hardkernel,odroid-c2)
        OVERLAY_MODE='patch'
        INCLUDE_FILE='/var/lib/piVCCU/dts/odroidc2.dts.include'
        FDT_FILE=amlogic/meson-gxbb-odroidc2.dtb
        break
        ;;
      libretech,cc)
        OVERLAY_MODE='patch'
        INCLUDE_FILE='/var/lib/piVCCU/dts/lepotato.dts.include'
        FDT_FILE=amlogic/meson-gxl-s905x-libretech-cc.dtb
        break
        ;;
     friendlyelec,nanopc-t4)
        OVERLAY_MODE='patch'
        INCLUDE_FILE='/var/lib/piVCCU/dts/nanopct4.dts.include'
        break
        ;; 
     friendlyelec,nanopi-m4)
        OVERLAY_MODE='patch'
        INCLUDE_FILE='/var/lib/piVCCU/dts/nanopim4.dts.include'
        break
        ;;
    esac
  done
fi

case "$OVERLAY_MODE" in
  patch)
    CONFIG_FDT_FILE=`grep -e '^fdt_file=' /boot/armbianEnv.txt | cut -d= -f2`
    if [ ! -z "$CONFIG_FDT_FILE" ]; then
      FDT_FILE=$CONFIG_FDT_FILE
    fi

    CONFIG_FDT_FILE=`grep -e '^fdtfile=' /boot/armbianEnv.txt | cut -d= -f2`
    if [ ! -z "$CONFIG_FDT_FILE" ]; then
      FDT_FILE=$CONFIG_FDT_FILE
    fi

    if [ -z "$FDT_FILE" ]; then
      echo "piVCCU: Error! Current FDT could not be determined"
      exit
    fi

    if [ -z "$INCLUDE_FILE" ]; then
      echo "piVCCU: Error! Hardware platform is not supported"
      exit
    fi
    ;;

  overlay)
    if [ -z "$OVERLAY_FILE" ]; then
      echo "piVCCU: Error! Hardware platform is not supported"
      exit
    fi
    OVERLAY=`basename "$OVERLAY_FILE" .dtbo`
    ;;

  *)
    echo "piVCCU: Error! Hardware platform is not supported"
    exit
    ;;
esac
