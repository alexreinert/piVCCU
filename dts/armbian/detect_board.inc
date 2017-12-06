if [ `grep -c -e 'asus,rk3288-tinker\|rockchip,rk3288-miniarm' /proc/device-tree/compatible` -eq 1 ]; then
  OVERLAY_MODE='patch'
  INCLUDE_FILE='/var/lib/piVCCU/dts/tinkerboard.dts.include'
fi
if [ `grep -c -e 'xunlong,orangepi-one\|xunlong,orangepi-plus' /proc/device-tree/compatible` -eq 1 ]; then
  OVERLAY_MODE='overlay'
  OVERLAY_FILE='pivccu-sun8i-h3.dtbo'
fi


if [ ! -z "$1" ]; then
  OVERLAY_MODE="$1"

  case "$OVERLAY_MODE" in
    patch)
      INCLUDE_FILE="$2"
      ;;
    overlay)
      OVERLAY_FILE="$2"
      ;;
  esac
fi

case "$OVERLAY_MODE" in
  patch)
    if [ -e /boot/armbianEnv.txt ]; then
      FDT_FILE=`grep -e '^fdt_file=' /boot/armbianEnv.txt | cut -d= -f2`
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
