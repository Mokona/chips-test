2 6144 Ad "                                            " Wn "TESTMEMFETCH-1" ns  I ² 16 ¤ 12 © «1 |x  36864,I É "THE FIRST TWO COLUMNS IN LINE 1+2 SHOULD BE DIFFERENT          " Û  (I±14) 155 ) "THE SECOND AND THIRD  COLUMN IN LINE 1+2    SHOULD BE DIFFERENT " ;  (I±12) 155  "THE THIRD AND FOURTH  COLUMN IN LINE 1+2    SHOULD BE DIFFERENT "  1000    ç ±è"PRESS KEY" Åò¡A$:A$²""1010 Ëü   I 0   Aû  tarting the reference timer
  bne *-1
  stx $9115     ; start the reference timer

pointers:
  lda #<irq     ; set the raster IRQ routine pointer
  sta $314
  lda #>irq
  sta $315
  lda #$c0
  sta $912e     ; enable Timer A underflow interrupts
  rts           ; return

irq:
; irq (event)   ; > 7 + at least 2 cycles of last instruction (9 to 16 total)
; pha           ; 3
; txa           ; 2
; pha           ; 3
; tya           ; 2
; pha           ; 3
; tsx           ; 2
; lda $0104,x   ; 4
; and #xx       ; 2
; beq           ; 3
; jmp ($314)    ; 5
                ; ---
                ; 38 to 45 cycles delay at this stage

  lda $9114     ; get the NMI timer A value
                ; (42 to 49 cycles delay at this stage)
; sta $1e00     ; uncomment these if you want to monitor
; ldy $9115     ; the reference timer on the screen
; sty $1e01
  cmp #8        ; are we more than 7 cycles ahead of time?
  bcc 0$
  pha           ; yes, spend 8 extra cycles
  pla
  and #7        ; and reset the high bit
0$:
  cmp #4
  bcc 1$
  bit $24       ; waste 4 cycles
  and #3
1$:
  cmp #2        ; spend the rest of the cycles
  bcs *+2
  bcs *+2
  lsr
  bcs *+2       ; now it has taken 82 cycles from the beginning of the IRQ

effect:
  ldy #16       ; perform amazing video effect
  lda $1e00
  tax
  eor #$f7
0$:
  sta $1e00
  stx $1e00
  sta $9600
  stx $9600
  sta $400f
  stx $400f
  sta $400f
  stx $400f
  sta $400f
  stx $400f
  sta $400f
  stx $400f
  pha
  pla
#if SYSTEM & PAL
  pha
  pla©.-¢ìÐû 	$$®$$¢ÊÐýÍ° Ðí©@+©¢V& Ðýêê% 
Ðý©]©©À.`­ÉHh)É$$)É° ° J°  ­ªI@@@@@@@@@@HhHhêêÐÓL¿ê