# ---------------------------------------------------
# NXRuby 機能テスト用スクリプト (完全版)
# ---------------------------------------------------

Window.caption = "NXRuby Engine Test"
Window.width   = 640
Window.height  = 480
Window.scale   = 1.0

px = 320.0
py = 240.0

Window.loop do
  # 矢印キーで移動
  px += Input.x * 5
  py += Input.y * 5

  # タイトルバーに現在の座標をリアルタイム表示
  Window.caption = "NXRuby - px:#{px.to_i} py:#{py.to_i} l_fps:#{Window.logic_fps.to_i} r_fps#{Window.render_fps.to_i}"

  # [SPACE] フルスクリーンの切り替え
  if Input.key_push?(Input::K_SPACE)
    Window.full_screen = !Window.full_screen?
  end

  # [ESC] アプリの終了
  if Input.key_push?(Input::K_ESCAPE)
    Window.close
  end

  # [Z][X] 画面スケール（拡大率）の変更
  if Input.key_push?(Input::K_Z)
    Window.scale = 1.0 # 等倍
  elsif Input.key_push?(Input::K_X)
    Window.scale = 2.0 # 2倍
  end

  # カメラ（オフセット）の更新
  Window.ox = px - (Window.width / 2.0)
  Window.oy = py - (Window.height / 2.0)

  # マウステスト（生の座標にカメラのox/oyを足す）
  mx = Input.mouse_x + Window.ox
  my = Input.mouse_y + Window.oy
  
  # カメラ位置に合わせて無限に続くグリッドを描画
  start_x = (Window.ox / 100).to_i * 100
  start_y = (Window.oy / 100).to_i * 100

  # 縦線を引く（x座標を100ずつズラしながらループ）
  x = start_x - 100
  while x <= start_x + Window.width + 100
    Window.draw_line(x, Window.oy - 100, x, Window.oy + Window.height + 100, [50, 50, 50])
    x += 100
  end

  # 横線を引く（y座標を100ずつズラしながらループ）
  y = start_y - 100
  while y <= start_y + Window.height + 100
    Window.draw_line(Window.ox - 100, y, Window.ox + Window.width + 100, y, [50, 50, 50])
    y += 100
  end

  if Input.mouse_down?
    Window.draw_rect_fill(mx - 10, my - 10, 20, 20, [255, 100, 100])
  else
    Window.draw_rect(mx - 10, my - 10, 20, 20, [255, 255, 255])
  end

  # プレイヤーの描画
  Window.draw_circle_fill(px, py, 20, [100, 255, 100])
end