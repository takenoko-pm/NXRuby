# examples/app.rb

# --- ウィンドウの初期設定 ---
Window.caption = "NXRuby Block Breaker"
Window.width = 640
Window.height = 480
Window.fps = 60

# --- ゲーム状態の初期化メソッド ---
# ゲームオーバーやクリア時にリセットしやすいようにメソッド化しておきます
def init_game
  {
    state: :playing, # :playing, :gameover, :clear
    paddle: { x: 320 - 40, y: 440, w: 80, h: 10, speed: 6, color: [50, 255, 100] },
    ball:   { x: 320, y: 300, r: 6, dx: 4.0, dy: -4.0, color: [255, 255, 255] },
    blocks: create_blocks
  }
end

# --- ブロック群の生成メソッド ---
def create_blocks
  blocks = []
  rows = 5
  cols = 10
  block_w = 52
  block_h = 20
  offset_x = (640 - (cols * block_w)) / 2  # 画面中央に配置されるよう計算
  offset_y = 50

  rows.times do |row|
    cols.times do |col|
      blocks << {
        x: offset_x + col * block_w,
        y: offset_y + row * (block_h + 5), # 5pxの隙間を空ける
        w: block_w - 4,
        h: block_h,
        color: [255 - row * 40, 100 + row * 20, 255], # 行ごとに色を変える
        alive: true
      }
    end
  end
  blocks
end

# 初期化を実行して変数に格納
game = init_game

# --- メインループ ---
Window.loop do
  if game[:state] == :playing
    # ==========================================
    # 更新処理
    # ==========================================
    
    # --- パドルの移動 ---
    # エンジン側の Input.x は -1, 0, 1 を返すので非常に扱いやすいです
    game[:paddle][:x] += Input.x * game[:paddle][:speed]
    
    # パドルが画面外に出ないようにクランプ
    game[:paddle][:x] = 0 if game[:paddle][:x] < 0
    game[:paddle][:x] = 640 - game[:paddle][:w] if game[:paddle][:x] + game[:paddle][:w] > 640

    # --- ボールの移動 ---
    game[:ball][:x] += game[:ball][:dx]
    game[:ball][:y] += game[:ball][:dy]

    # --- ボールと壁の衝突判定 ---
    # 左右の壁
    if game[:ball][:x] - game[:ball][:r] < 0 || game[:ball][:x] + game[:ball][:r] > 640
      game[:ball][:dx] *= -1
      # 壁にめり込まないように補正
      game[:ball][:x] = game[:ball][:r] if game[:ball][:x] - game[:ball][:r] < 0
      game[:ball][:x] = 640 - game[:ball][:r] if game[:ball][:x] + game[:ball][:r] > 640
    end

    # 天井
    if game[:ball][:y] - game[:ball][:r] < 0
      game[:ball][:dy] *= -1
      game[:ball][:y] = game[:ball][:r]
    end

    # 画面下部（ゲームオーバー判定）
    if game[:ball][:y] + game[:ball][:r] > 480
      game[:state] = :gameover
    end

    # --- ボールとパドルの衝突判定 ---
    p = game[:paddle]
    b = game[:ball]
    if b[:y] + b[:r] > p[:y] && b[:y] - b[:r] < p[:y] + p[:h] &&
       b[:x] + b[:r] > p[:x] && b[:x] - b[:r] < p[:x] + p[:w]
       
      b[:dy] *= -1
      b[:y] = p[:y] - b[:r] # めり込み防止
      
      # 当たった位置（中心からどれくらいズレているか）で反射角（dx）を変化させる
      hit_pos = (b[:x] - (p[:x] + p[:w] / 2.0)) / (p[:w] / 2.0)
      b[:dx] = hit_pos * 5.0
    end

    # --- ボールとブロックの衝突判定 ---
    game[:blocks].each do |block|
      next unless block[:alive] # 壊れたブロックは無視
      
      # 簡易的な矩形 vs 円の当たり判定
      if b[:x] + b[:r] > block[:x] && b[:x] - b[:r] < block[:x] + block[:w] &&
         b[:y] + b[:r] > block[:y] && b[:y] - b[:r] < block[:y] + block[:h]
         
        block[:alive] = false
        b[:dy] *= -1 # 反射
        break # 1フレームにつき1つのブロックだけ処理する
      end
    end

    # --- クリア判定 ---
    if game[:blocks].all? { |block| !block[:alive] }
      game[:state] = :clear
    end

  else
    # ==========================================
    # ゲームオーバー・クリア時の処理
    # ==========================================
    
    # スペースキーを押した瞬間（Input.key_push?）にリセット
    if Input.key_push?(Input::K_SPACE)
      game = init_game
    end
  end

  # ==========================================
  # 描画処理
  # ==========================================
  
  # 背景はエンジン側の Window.bgcolor(デフォルト黒) によって自動クリアされます

  # ブロックの描画
  game[:blocks].each do |block|
    if block[:alive]
      Window.draw_rect_fill(block[:x], block[:y], block[:w], block[:h], block[:color])
    end
  end

  # パドルの描画
  Window.draw_rect_fill(game[:paddle][:x], game[:paddle][:y], game[:paddle][:w], game[:paddle][:h], game[:paddle][:color])
  
  # ボールの描画
  Window.draw_circle_fill(game[:ball][:x], game[:ball][:y], game[:ball][:r], game[:ball][:color])

  # 状態に応じたオーバーレイ描画 (文字の代わり)
  if game[:state] == :gameover
    # 赤い半透明のフィルターをかける (アルファ値対応が活きます！)
    Window.draw_rect_fill(0, 0, 640, 480, [255, 0, 0, 100])
  elsif game[:state] == :clear
    # 黄色い半透明のフィルターをかける
    Window.draw_rect_fill(0, 0, 640, 480, [255, 255, 0, 100])
  end
end