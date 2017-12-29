if [ -e /proc/device-tree/compatible ]; then
  for str in `strings /proc/device-tree/compatible`; do
    case $str in
      xunlong,orangepi-one|xunlong,orangepi-lite|xunlong,orangepi-plus|xunlong,orangepi-plus2e|xunlong,orangepi-2|xunlong,orangepi-pc|xunlong,orangepi-pc-plus)
        OVERLAY_MODE='overlay'
        OVERLAY_FILE='pivccu-sun8i-h3.dtbo'
        break
        ;;
      rockchip,rk3288-miniarm|asus,rk3288-tinker)
        OVERLAY_MODE='patch'
        INCLUDE_FILE='/var/lib/piVCCU/dts/tinkerboard.dts.include'
        break
        ;;
    esac
  done
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
