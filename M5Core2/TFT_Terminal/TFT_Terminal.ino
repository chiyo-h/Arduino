/***************************************************
 * ・この動作確認用programでは、M5Core2を使ってハードウエアスクロールを確認する。
 * ・オリジナルは、TFT_eSPIライブラリのサンプルプログラムである。
 * ・M5Stack Core2で動作するように変更した。
 * 
 * ハードウエアスクロールを確認するためにTFT_eSPIライブラリのサンプルプログラムを実行。
 * M5Core2.hで動作しようとしたところでハマりまくった。
 *
 * 結論：
 *       罠１
 *         M5Stack Core2のLCDコントロールICは、ILITEK ILI9341ではなく『ILI9342C』らしい。
 *       液晶パネルを剥いて確かめてないのでわからないがM5Stackシリーズは前からこれみたい
 *       だ。ホントになってコッタい。M5公式のページから指定されているのはILI9341のデータ
 *       シートだから。その仕様書を読んで解析してたのに。
 * 
 *       罠２
 *         hardscrollコマンドは、１方向固定らしい。つまり、ILI9341は、縦長方向。ILI9342C
 *       は、横長方向の時のみ有効。実際は、ILI9342C 240lineスクロールになっていてオリジナル
 *       サンプルプログラムは、ILI9341用なので240x320で縦長default設定320lineスクロールに
 *       なっている。このことは、サンプルのコメントにはsetRotation(0)じゃないと動かないと
 *       書いてある。が実際は0,2,4,6の縦長でないと動かないである。どちらにしても、M5Stack
 *       では動作しない。
 * 
 *       罠３
 *         M5StackシリーズのTFT_eSPI液晶ドライバ定義 In_eSPI_Setup.hでILI9341_DRIVERを
 *       指定している。ILI9342Cは、ILI9341の亜種扱いで一部パラメータを変更する程度で対応
 *       できるので、ILI9342C_DRIVERがないのであろう。ゆえに、勘違いする。
 * 
 *       罠４
 *         ハマった原因。サンプルプログラムを環境に合わせて変更し、一見動作している
 *       ように見えるが、どう動いたら正解なのかわからないのが問題。いや、サンプル
 *       プログラムは確実に動かないと問題なのだ。Serialからのデータ入力は、画面に
 *       映っているがなにをハードスクロールしているか不明であった。
 * 
 */
#include <M5Core2.h>

// The scrolling area must be a integral multiple of FONT_HEIGHT
// スクロール領域はFONT_HEIGHTの整数倍でなければなりません

#define FONT_HEIGHT 16    // print およびscroll するテキストの高さ
#define BOT_FIXED_AREA 16*1 // 下部固定領域の行数（画面の下から数えて行数）
#define TOP_FIXED_AREA 16*1 // 上部固定領域の行数（画面上部から数えて行数）
#define LCD_HEIGHT 240    // M5Stack LCD 高さサイズ 240 pixel
#define LCD_WIDTH 320     // M5Stack LCD 幅サイズ   320 pixel

uint16_t lcd_h = LCD_HEIGHT;
uint16_t lcd_w = LCD_WIDTH;
uint16_t txt_h = (LCD_HEIGHT / FONT_HEIGHT);
uint16_t txt_w = (LCD_WIDTH / FONT_HEIGHT);

// The initial y coordinate of the top of the scrolling area
// (TFA) top fixed area
// スクロール領域の上部の最初のy座標 
uint16_t yStart = TOP_FIXED_AREA;  // 16

// yArea must be a integral multiple of FONT_HEIGHT
// (VSA) vertical scrolling area
// yAreaはFONT_HEIGHTの整数倍でなければなりません
uint16_t yArea = LCD_HEIGHT - TOP_FIXED_AREA-BOT_FIXED_AREA;  // 320 - 16 - 0

// The initial y coordinate of the top of the bottom text line
// (BFA) bottom fixed area
// 下部のテキスト行の上部の最初のy座標
uint16_t yDraw = LCD_HEIGHT - BOT_FIXED_AREA - FONT_HEIGHT;  // 320 - 0 - 16


// TOP HEADERが何行当たりになるか
uint16_t txt_hed = (TOP_FIXED_AREA / FONT_HEIGHT);
uint16_t txt_fot = (BOT_FIXED_AREA / FONT_HEIGHT);

// Keep track of the drawing x coordinate
// 図面のx座標を追跡します
uint16_t xPos = 0;

// For the byte we read from the serial port
// シリアルポートから読み取ったバイト
byte data = 0;

// A few test variables used during debugging
// デバッグ中に使用されるいくつかのテスト変数
//boolean change_colour = 1;
//boolean selected = 1;

// We have to blank the top line each time the display is scrolled, but this takes up to 13 milliseconds
// for a full width line, meanwhile the serial buffer may be filling... and overflowing
// ディスプレイをスクロールするたびに一番上の行を空白にする必要がありますが、
// これには全幅の行で最大13ミリ秒かかります。その間、シリアルバッファがいっぱいになり
// オーバーフローする可能性があります。

// We can speed up scrolling of short text lines by just blanking the character we drew
// 描いた文字を空白にするだけで、短いテキスト行のスクロールを高速化できます

// We keep all the strings pixel lengths to optimise the speed of the top line blanking
// トップラインブランキングの速度を最適化するために、すべての文字列のピクセル長を維持します
// 320, 240で長いのは320である。今回
// (320 - 16) / 16 = 19  TOPに1行HEADERがあるので20-1になっている
// LCD_WIDTH/FONT_HEIGHT  320>240  320/16 = 20 が最大行0~19
int blank[20];

// 画面の回転と属性番号
int rot = 0;

// スクロールするしない
boolean scroll = 1;

// ##############################################################################################
// Call this function to scroll the display one text line
// この関数を呼び出して、表示を1テキスト行スクロールします
// 改行処理
// yStart(FRAME RAM座標)は、スクロール開始ラインのことなので、LCD座標で、TFAの次のラインになる。
// 今回のスクロールは、スクロール行がVSA(240-TFA-BFA)ラインと決まっていて、縦ラインがこの大きさ
// のプリンタ用紙とたとえると。用紙の上と下をリングにしてつなげた格好になっている。
// 位置関係としては、LCD座標で、最終行を常に文字を書いていく。スクロールは上側に回す。
// LCD座標の一番上の行は上スクロールで消去される。なので、FRAME RAM座標では、左上から印字して改行に
// なったら、次の行を１行消去して。yStartを1行分進めることで用紙全体が上側にスクロールされる。
// 改行して消去した行に印字していく。というようなことが行われている。なので、スクロール自体されないと
// LCD上に左上から表示され、右下まで行ったら左上に移動して印字が続くイメージになる。
// ※1行＝フォントの高さ(16ライン)
// ##############################################################################################
int scroll_line() {
  // Store the old yStart, this is where we draw the next line
  // 古いyStartを保存します。ここに、次の線を引きます。
  int yTemp = yStart;  // 次の文字Y軸になる

  // Use the record of line lengths to optimise the rectangle size we need to erase the top line
  // 行の長さの記録を使用して、一番上の行を消去するために必要な長方形のサイズを最適化します
  // スクロールエリアの左上から１行分塗りつぶす
  // 左上座標(0, yStart) w = blank配列に記録されているx座標(最適化) h=文字の高さ
  if(blank[yStart/FONT_HEIGHT] !=0){
    //M5.Lcd.fillRect(0, yStart, lcd_w-1, FONT_HEIGHT, TFT_BLACK); // debug 1行全部clear
    //M5.Lcd.fillRect(0, yStart, blank[yStart/FONT_HEIGHT], FONT_HEIGHT, TFT_RED);
    M5.Lcd.fillRect(0, yStart, blank[yStart/FONT_HEIGHT], FONT_HEIGHT, TFT_BLACK);
  }
  blank[yStart/FONT_HEIGHT] = 0;  // リセット bug? 改行の時blank位置がリセットされない。

  Serial.printf("fillRect %d, %d, %d, %d\n", 
                  0, yStart, blank[yStart/FONT_HEIGHT], FONT_HEIGHT);

  // Change the top of the scroll area
  // スクロール領域の上部を変更します
  yStart += FONT_HEIGHT;  // +1行する

  // The value must wrap around as the screen memory is a circular buffer
  // 画面メモリは循環バッファであるため、値はラップアラウンドする必要があります
  // yStart >= (320 - 0) より大きければ 0 + yStart - (320 - 0)
  // > にすると208行目が発生する

  if (yStart >= (lcd_h - BOT_FIXED_AREA)){
    yStart = yStart - (lcd_h - TOP_FIXED_AREA - BOT_FIXED_AREA);
  }

  // Now we can scroll the display
  // これで、ディスプレイをスクロールできます
  Serial.printf("scrollAddress yTemp:%d yStart:%d,(xPosy,Draw)=(%d,%d)\n", yTemp, yStart, xPos, yDraw);
  scrollAddress(yStart);  // yStart→TOP line

  return  yTemp; // 次の文字Y軸になる
}

// ##############################################################################################
// Setup a portion of the screen for vertical scrolling
// 画面の一部を垂直スクロール用に設定します
// ##############################################################################################
// We are using a hardware feature of the display, so we can only scroll in portrait orientation
// ディスプレイのハードウェア機能を使用しているため、縦向きでのみスクロールできます
void setupScrollArea(uint16_t tfa, uint16_t bfa) {  // 16,0 が設定されている

  // 強引に縦でハードスクロールするなら設定の合計を240にすれば動く
  // その場合、スクロール自体は、240line方向にスクロールする。
  /*
  if((rot&1) == 0){

    M5.Lcd.writecommand(ILI9341_VSCRDEF);
    M5.Lcd.writedata(0);   // 上位8bit
    M5.Lcd.writedata(0);   // 下位8bit
    M5.Lcd.writedata(0);   // 上位8bit
    M5.Lcd.writedata(240); // 下位8bit
    M5.Lcd.writedata(0);   // 上位8bit
    M5.Lcd.writedata(0);   // 下位8bit

  }else{
  */
    // Vertical scroll definition
    // 垂直スクロールの定義
    M5.Lcd.writecommand(ILI9341_VSCRDEF);

    // Top Fixed Area line count
    // トップ固定エリアライン数
    M5.Lcd.writedata(tfa >> 8);  // 上位8bit
    M5.Lcd.writedata(tfa);       // 下位8bit

    // Vertical Scrolling Area line count
    // 垂直スクロール領域の行数
    M5.Lcd.writedata((lcd_h - tfa - bfa)>>8);  // 上位8bit
    M5.Lcd.writedata(lcd_h - tfa - bfa);       // 下位8bit

    // Bottom Fixed Area line count
    // 下部固定領域の行数
    M5.Lcd.writedata(bfa >> 8);  // 上位8bit
    M5.Lcd.writedata(bfa);       // 下位8bit
  /*
  }
  */
  /*****************************
   * 仕様書の例通りやってみる

  // CASET Column Address
  M5.Lcd.writecommand(ILI9341_CASET);
  M5.Lcd.writedata(0);                // 上位8bit
  M5.Lcd.writedata(0);                // 下位8bit
  M5.Lcd.writedata((lcd_w -1) >> 8);  // 上位8bit
  M5.Lcd.writedata(lcd_w -1);         // 下位8bit

  // PASET Page Address
  M5.Lcd.writecommand(ILI9341_PASET);
  M5.Lcd.writedata(0);  // 上位8bit
  M5.Lcd.writedata(0);       // 下位8bit
  M5.Lcd.writedata((lcd_h -1) >> 8);  // 上位8bit
  M5.Lcd.writedata(lcd_h -1);       // 下位8bit

  M5.Lcd.setRotation(rot); // 回転設定

  */

}

// ##############################################################################################
// Setup the vertical scrolling start address pointer
// 垂直スクロール開始アドレスポインタを設定します
// VSPラインがTOP or bottom line に移動みたいな動き
// MADCTL D4 =0(TOP)/1(BOTTOM)
// ##############################################################################################
void scrollAddress(uint16_t vsp) {

  // Vertical scrolling pointer
  // 垂直スクロールポインタ
  M5.Lcd.writecommand(ILI9341_VSCRSADD);

  if(scroll ==0) vsp = TOP_FIXED_AREA;

  M5.Lcd.writedata(vsp>>8);  // 上位8bit
  M5.Lcd.writedata(vsp);     // 下位8bit

  M5.Lcd.setCursor(0, (txt_h -1)*FONT_HEIGHT);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);
  M5.Lcd.printf(" TFA:%3d VSA:%3d BFA:%3d VPS:%3d   ", TOP_FIXED_AREA, lcd_h - TOP_FIXED_AREA - BOT_FIXED_AREA,
                                                       BOT_FIXED_AREA, vsp);
  Serial.printf(" TFA:%3d VSA:%3d BFA:%3d VPS:%3d\n",  TOP_FIXED_AREA, lcd_h - TOP_FIXED_AREA - BOT_FIXED_AREA,
                                                       BOT_FIXED_AREA, vsp);
  /*
  if(selected == 1){
    M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  }else{
    M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
  }
  */
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);
}

void setup() {

  M5.begin();

  // Must be setRotation(0) for this sketch to work correctly
  // このスケッチが正しく機能するには、setRotation(0)である必要があります
  // M5Stackの場合は、1の方縦長だとHARDスクロールが効かない。
  rot = 1;

  // 縦長用
  if((rot&0x01) == 0){
    lcd_h = LCD_WIDTH;  // 320
    lcd_w = LCD_HEIGHT;  // 240
    txt_h = lcd_h / FONT_HEIGHT ;
    txt_w = lcd_w / FONT_HEIGHT ;
    yArea = lcd_h - TOP_FIXED_AREA - BOT_FIXED_AREA;  // 320 - 16 - 0
    yDraw = lcd_h - BOT_FIXED_AREA - FONT_HEIGHT;     // 320 - 0 - 16
  }

  M5.Lcd.setRotation(rot); // 0:-90°回転 1:0°
  M5.Lcd.fillScreen(TFT_BLACK);
 
  // Setup baud rate and draw top banner
  //Serial.begin(9600);

  // ヘッダ部分ここは、スクロール対象外
  //ヘッダ、フッターの出力
  M5.Lcd.setTextFont(2);
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLUE);

  M5.Lcd.fillRect(0, 0, lcd_w-1, TOP_FIXED_AREA -1, TFT_BLUE);
  M5.Lcd.drawCentreString("Serial Terminal", lcd_w/2, 0, 2);

  M5.Lcd.fillRect(0, (txt_h - txt_fot)*FONT_HEIGHT, lcd_w-1, lcd_h-1, TFT_BLUE);
  M5.Lcd.setCursor(0, (txt_h-1)*FONT_HEIGHT);
  M5.Lcd.printf(" TFA:%3d VSA:%3d BFA:%3d VPS:%3d",
                TOP_FIXED_AREA, (lcd_h - TOP_FIXED_AREA - BOT_FIXED_AREA), BOT_FIXED_AREA, 0);

  // Change colour for scrolling zone text
  // ゾーンテキストをスクロールするための色を変更する
  M5.Lcd.setTextColor(TFT_WHITE, TFT_BLACK);


  // Setup scroll area
  // スクロールエリアの設定
  // (16,32) 上(TOP)から16+1line目から下(BOTTOM)32+1line目まで
  setupScrollArea(TOP_FIXED_AREA, BOT_FIXED_AREA);

  // Zero the array 配列をゼロにする
  // blank[0] ~ blank[19] = 0
  for (byte i = 0; i<20; i++) blank[i]=0;

}

void loop(void) {

  //  These lines change the text colour when the serial buffer is emptied
  //  これらの行は、シリアルバッファが空になるとテキストの色を変更します
  //  These are test lines to see if we may be losing characters
  //  これらは、文字が失われる可能性があるかどうかを確認するためのテスト行です
  //  Also uncomment the change_colour line below to try them
  //  また、下のchange_colour行のコメントを外して試してください
  /*
  if(change_colour){
    change_colour = 0;
 
    if(selected == 1){
      M5.Lcd.setTextColor(TFT_CYAN, TFT_BLACK);
      selected = 0;
    }else{
      M5.Lcd.setTextColor(TFT_MAGENTA, TFT_BLACK);
      selected = 1;
    }
  }
  */

  // RXbufferが無かったらloop関数から抜けて一度外部処理に戻る
  while (Serial.available()) {

    // Serial から1文字入力
    data = Serial.read();

    // *** 遊び ***
    M5.Lcd.drawLine( 0, 0, lcd_w, lcd_h, BLUE);
    M5.Lcd.drawLine( 0, lcd_h, lcd_w, 0, BLUE);
    // くるくるスクロール uとdを連打するとくるくるスクロールする。
    if (data == '/'){
      for(int i=0;i<lcd_h;i++) scrollAddress(i);
      scrollAddress(yStart);
    }
    if (data == '\\'){
      for(int i=lcd_h;i>=0;i--) scrollAddress(i);
      scrollAddress(yStart);
    }

    // *** SCROLL ON/OFF ***
    if(data == '_'){
      if(scroll ==1){
        scroll = 0;
      }else{
        scroll = 1;
      }
    }

    // *** 改行処理 ***
    // If it is a CR or we are near end of line then scroll one line
    // CRの場合、または行の終わりに近づいている(231)場合は、1行スクロールします
    // 240 - 231 = 9 font#2は、max width 10なので、9まで 
    if (data == '\r' || xPos>(lcd_w - 9)) {
      xPos = 0;
      // It can take 13ms to scroll and blank 16 pixel lines
      // スクロールして16ピクセルの行を空白にするのに13ミリ秒かかる場合があります
      yDraw = scroll_line(); 
    }

    // *** 文字描画 ***
    // 入力データが文字範囲なら、描画する。
    // SP(32) ~ DEL(127)まで
    if (data > 31 && data < 128) {
      Serial.printf("drawChar (%d,%d) %c(%d) ", xPos, yDraw, data, data);
      xPos += M5.Lcd.drawChar(data, xPos, yDraw, 2);
      Serial.printf("blank[%d]=%d\n", yDraw/FONT_HEIGHT, xPos);


      // Keep a record of line lengths
      // 描画するたびにxPosの位置を更新する。
      // 行の長さを記録する
      // 19で0になるから、0~18の繰り返し
      // ( 18 + (yStart-TOP_FIXED_AREA) /FONT_HEIGHT) %19
      // (18 + (yStart/16 -1) )%19
      // (18行(最大行-TOP_FIXED_AREA) + (yStart/16)-1行 )%19行(最大行)
      //blank[(18+(yStart-TOP_FIXED_AREA)/FONT_HEIGHT)%19]=xPos;
      blank[yDraw/FONT_HEIGHT]=xPos;

    }

    // Line to indicate buffer is being emptied
    // バッファが空になっていることを示す行
    //change_colour = 1; 

  }
}

/*************************************************************
  This sketch implements a simple serial receive terminal
  program for monitoring serial debug messages from another
  board.
  
  Connect GND to target board GND
  Connect RX line to TX line of target board
  Make sure the target and terminal have the same baud rate
  and serial stettings!

  The sketch works with the ILI9341 TFT 240x320 display and
  the called up libraries.
  
  The sketch uses the hardware scrolling feature of the
  display. Modification of this sketch may lead to problems
  unless the ILI9341 data sheet has been understood!

  Updated by Bodmer 21/12/16 for TFT_eSPI library:
  https://github.com/Bodmer/TFT_eSPI
  
  BSD license applies, all text above must be included in any
  redistribution
 *************************************************************/
