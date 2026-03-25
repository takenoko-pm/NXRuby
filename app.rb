ball_x, ball_y = 320, 240
ball_r = 15
ball_dx, ball_dy = 5, 5
Window.fps = 120

Window.loop do
  # 計算
  ball_x += ball_dx
  ball_y += ball_dy
  # 壁反射
  ball_dx *= -1 if ball_x < 0 || ball_x > 640
  ball_dy *= -1 if ball_y < 0 || ball_y > 480

  # 描画
  # 塗りつぶし円でボールを描く
  Window.draw_circle_fill(ball_x, ball_y, ball_r, [255, 255, 0])
  # 枠線円でエフェクト
  Window.draw_circle(ball_x, ball_y, ball_r + 5, [255, 255, 255])
  
  # FPSを表示
  Window.caption = "NXRuby - FPS: #{Window.logic_fps.to_i}"
end