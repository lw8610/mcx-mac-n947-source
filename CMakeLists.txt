cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(mcx_mac)

# The public variant must neither link the legacy floppy driver nor embed
# private media. The post-build map/asset audit remains mandatory.
option(MCX_MAC_PUBLIC_RELEASE "Build an asset-free, release-audited image" ON)
if(MCX_MAC_PUBLIC_RELEASE AND
   (NOT CONFIG_MCX_MAC_UMAC_BOOT OR
    NOT CONFIG_MCX_MAC_UMAC_MEDIA_FROM_SLOT1 OR
    NOT CONFIG_MCX_MAC_INDEPENDENT_FLOPPY))
  message(FATAL_ERROR
    "Public uMac build requires boot + separate media slot + independent floppy. See RELEASE_STATUS.md")
endif()

if(MCX_MAC_PUBLIC_RELEASE AND CONFIG_MCX_MAC_USB_IMAGE_TRANSFER)
  message(FATAL_ERROR
    "Experimental USB transfer mode uses a test VID/PID and is not release-ready")
endif()

if(MCX_MAC_PUBLIC_RELEASE AND
   (CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE OR CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE))
  message(FATAL_ERROR "Diagnostic opcode profiling must not be included in a public build")
endif()

if(MCX_MAC_PUBLIC_RELEASE AND CONFIG_MCX_MAC_UMAC_MOVEM_MASK_ITER)
  message(FATAL_ERROR "Experimental MOVEM optimization is not release-validated")
endif()

if(MCX_MAC_PUBLIC_RELEASE AND CONFIG_MCX_MAC_UMAC_CPU_CONTEXT_RAMX)
  message(FATAL_ERROR "Experimental Musashi CPU context placement is not release-validated")
endif()

set(MCUX_CTIMER_DIR
  ${ZEPHYR_BASE}/../modules/hal/nxp/mcux/mcux-sdk-ng/drivers/ctimer)

target_include_directories(app PRIVATE
  src
  ${MCUX_CTIMER_DIR}
)

target_sources(app PRIVATE
  src/main.c
  src/emulator/m68k_bus.c
  src/emulator/m68k_bus_diag.c
  src/emulator/m68k_musashi_bus.c
  src/emulator/umac_memory.c
  src/emulator/umac_timing.c
  src/emulator/umac_video.c
  src/video/vga.c
  src/video/vga_flexio.c
  src/video/vga_smartdma.c
  src/video/vga_diag.c
  ${MCUX_CTIMER_DIR}/fsl_ctimer.c
)

if(CONFIG_MCX_MAC_EXTERNAL_FLASH_PROBE)
  target_sources(app PRIVATE src/emulator/umac_external_flash_probe.c)
endif()

if(CONFIG_MCX_MAC_EXTERNAL_FLASH_HDD)
  target_sources(app PRIVATE src/emulator/umac_flash_hdd.c)
endif()

if(CONFIG_MCX_MAC_EXTERNAL_FLASH_FAT_IMAGE)
  target_sources(app PRIVATE src/emulator/umac_fat_image.c)
endif()

if(CONFIG_MCX_MAC_USB_IMAGE_TRANSFER)
  target_sources(app PRIVATE src/usb/umac_usb_transfer.c)
endif()

if(CONFIG_MCX_MAC_SMARTDMA_DIRECT)
  if(CONFIG_MCX_MAC_SMARTDMA_GUEST_DESCRIPTOR)
    target_sources(app PRIVATE
      src/video/vga_smartdma_guest_ring.c
      src/video/vga_smartdma_guest_descriptors.c)
  else()
    target_sources(app PRIVATE src/video/vga_smartdma_ring.c)
  endif()
endif()

if(CONFIG_MCX_MAC_UMAC_CORE)
  if(NOT (CONFIG_MCX_MAC_UMAC_RAM_KIB EQUAL 128 OR
          CONFIG_MCX_MAC_UMAC_RAM_KIB EQUAL 192 OR
          CONFIG_MCX_MAC_UMAC_RAM_KIB EQUAL 208))
    message(FATAL_ERROR "Mac RAM must be 128, 192, or 208 KiB; the private ROM must match")
  endif()
  set(UMAC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/umac)
  set(MUSASHI_DIR ${UMAC_DIR}/external/Musashi)
  set(UMAC_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/umac_generated)
  set(UMAC_GENERATOR ${UMAC_GEN_DIR}/m68kmake)

  if(CONFIG_MCX_MAC_UMAC_RAM_KIB GREATER 192)
    # This board initializes only one EHCI host. Move the second instance's
    # untouched reserve, plus the CPU-only system heap, to free contiguous
    # main SRAM for the guest. Keep active USB DMA buffers in main SRAM.
    zephyr_code_relocate(
      FILES ${ZEPHYR_BASE}/../modules/hal/nxp/mcux/mcux-sdk-ng/middleware/usb/host/usb_host_ehci.c
      LOCATION SRAMX_BSS
      FILTER "^\\.bss\\.s_UsbHostEhci(FrameList2|Data2)$")
    zephyr_code_relocate(
      FILES ${ZEPHYR_BASE}/kernel/mempool.c
      LOCATION SRAMX_NOINIT
      FILTER "^\\.noinit\\..*kheap_buf__system_heap$")
  endif()

  if(NOT EXISTS ${MUSASHI_DIR}/m68kmake.c)
    message(FATAL_ERROR "uMac/Musashi not found; clone external/umac with --recursive")
  endif()

  find_program(UMAC_HOST_CC NAMES cc clang)
  if(NOT UMAC_HOST_CC)
    message(FATAL_ERROR "A host C compiler is required to generate Musashi opcodes")
  endif()

  set(UMAC_HOT_DECORATE_COMMAND)
  set(UMAC_HOT_DECORATE_DEPENDS)
  if(CONFIG_MCX_MAC_UMAC_HOT_OPS_RAM OR CONFIG_MCX_MAC_UMAC_HOT_OPS_RAMX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(UMAC_HOT_DECORATE_COMMAND
      COMMAND ${Python3_EXECUTABLE} ${UMAC_DIR}/tools/decorate_ops.py
              ${UMAC_GEN_DIR}/m68kops.c ${UMAC_DIR}/tools/fn_hot200.txt)
    set(UMAC_HOT_DECORATE_DEPENDS
      ${UMAC_DIR}/tools/decorate_ops.py ${UMAC_DIR}/tools/fn_hot200.txt)
  endif()

  set(UMAC_MOVEM_OPTIMIZE_COMMAND)
  set(UMAC_MOVEM_OPTIMIZE_DEPENDS)
  if(CONFIG_MCX_MAC_UMAC_MOVEM_MASK_ITER)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(UMAC_MOVEM_OPTIMIZE_COMMAND
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/optimize_m68kops_movem.py
              ${UMAC_GEN_DIR}/m68kops.c)
    set(UMAC_MOVEM_OPTIMIZE_DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/tools/optimize_m68kops_movem.py)
  endif()

  add_custom_command(
    OUTPUT ${UMAC_GEN_DIR}/m68kops.c ${UMAC_GEN_DIR}/m68kops.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${UMAC_GEN_DIR}
    COMMAND ${UMAC_HOST_CC} -O2 ${MUSASHI_DIR}/m68kmake.c -o ${UMAC_GENERATOR}
    COMMAND ${UMAC_GENERATOR} ${UMAC_GEN_DIR} ${MUSASHI_DIR}/m68k_in.c
    ${UMAC_HOT_DECORATE_COMMAND}
    ${UMAC_MOVEM_OPTIMIZE_COMMAND}
    DEPENDS ${MUSASHI_DIR}/m68kmake.c ${MUSASHI_DIR}/m68k_in.c
            ${UMAC_HOT_DECORATE_DEPENDS} ${UMAC_MOVEM_OPTIMIZE_DEPENDS}
    VERBATIM
  )

  target_include_directories(app PRIVATE
    ${UMAC_DIR}/include
    ${MUSASHI_DIR}
    ${UMAC_GEN_DIR}
  )
  target_compile_definitions(app PRIVATE
    UMAC_MEMSIZE=${CONFIG_MCX_MAC_UMAC_RAM_KIB}
    # Keep the original 30-step mouse backlog. Shorter 68000 slices let
    # the emulated mouse issue more quadrature steps per wall-clock second.
    MCX_MAC_GUEST_LOOP_MS=1
    MCX_MAC_MOUSE_SUBSTEPS=2
    MCX_MAC_USB_MOUSE_RATE_MAP=1
    DISP_WIDTH=512
    DISP_HEIGHT=342
    MUSASHI_CNF=\"${CMAKE_CURRENT_SOURCE_DIR}/src/emulator/mcx_m68kconf.h\"
  )
  set(UMAC_M68KCPU_SOURCE ${MUSASHI_DIR}/m68kcpu.c)
  if(CONFIG_MCX_MAC_UMAC_OPCODE_PROFILE OR CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(UMAC_M68KCPU_SOURCE ${UMAC_GEN_DIR}/m68kcpu_profile.c)
    if(CONFIG_MCX_MAC_UMAC_CYCLE_PROFILE)
      set(UMAC_PROFILE_KIND cycles)
      target_sources(app PRIVATE src/emulator/mcx_cycle_profile.c)
    else()
      set(UMAC_PROFILE_KIND count)
      target_sources(app PRIVATE src/emulator/mcx_opcode_profile.c)
    endif()
    add_custom_command(
      OUTPUT ${UMAC_M68KCPU_SOURCE}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${UMAC_GEN_DIR}
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/instrument_m68kcpu.py
              ${MUSASHI_DIR}/m68kcpu.c ${UMAC_M68KCPU_SOURCE}
              ${UMAC_PROFILE_KIND}
      DEPENDS ${MUSASHI_DIR}/m68kcpu.c
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/instrument_m68kcpu.py
      VERBATIM
    )
  endif()
  if(CONFIG_MCX_MAC_UMAC_CPU_CONTEXT_RAMX)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(UMAC_M68KCPU_CONTEXT_INPUT ${UMAC_M68KCPU_SOURCE})
    set(UMAC_M68KCPU_SOURCE ${UMAC_GEN_DIR}/m68kcpu_context_ramx.c)
    add_custom_command(
      OUTPUT ${UMAC_M68KCPU_SOURCE}
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/place_m68kcpu_context_ramx.py
              ${UMAC_M68KCPU_CONTEXT_INPUT} ${UMAC_M68KCPU_SOURCE}
      DEPENDS ${UMAC_M68KCPU_CONTEXT_INPUT}
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/place_m68kcpu_context_ramx.py
      VERBATIM
    )
  endif()
  target_sources(app PRIVATE
    src/emulator/umac_block_backend.c
    src/emulator/umac_media_package.c
    ${UMAC_DIR}/src/main.c
    ${UMAC_DIR}/src/via.c
    ${UMAC_DIR}/src/scc.c
    ${UMAC_M68KCPU_SOURCE}
    ${MUSASHI_DIR}/m68kdasm.c
    ${UMAC_GEN_DIR}/m68kops.c
  )
  if(CONFIG_MCX_MAC_UMAC_HOT_OPS_RAMX)
    zephyr_code_relocate(
      FILES ${MUSASHI_DIR}/m68kcpu.c ${UMAC_GEN_DIR}/m68kops.c
      LOCATION SRAMXHOT
      FILTER "^\\.text\\.mcx_hot$")
  endif()
  if(CONFIG_MCX_MAC_UMAC_CPU_O2)
    set_source_files_properties(
      ${UMAC_M68KCPU_SOURCE}
      ${UMAC_GEN_DIR}/m68kops.c
      PROPERTIES COMPILE_OPTIONS "-O2")
  endif()
  target_sources(app PRIVATE src/emulator/umac_floppy.c)
  if(CONFIG_MCX_MAC_UMAC_SMOKE)
    target_sources(app PRIVATE src/emulator/umac_smoke.c)
  endif()
  if(CONFIG_MCX_MAC_UMAC_BOOT)
    target_sources(app PRIVATE
      src/emulator/umac_disk_overlay.c
      src/emulator/umac_serial_input.c)
    target_sources(app PRIVATE src/emulator/umac_media_slot_boot.c)
    if(CONFIG_MCX_MAC_USB_MOUSE)
      target_sources(app PRIVATE src/emulator/umac_usb_hid.c)
      target_include_directories(app PRIVATE ${ZEPHYR_BASE}/subsys/usb/host)
    endif()
  endif()
endif()
