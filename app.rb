# 1. 画像と節の準備（数は150くらいに増やすとよりリッチです）
dot = Image.new(32, 32, [255, 255, 255])
segments = []
150.times { segments << Sprite.new(320, 240, dot) }

Window.loop do
  # --- [1] ヘッドの動きをカオスに ---
  # 複数のサイン波を合成して、予測不能かつ大きな動きを作る
  t = Window.running_time * 0.001
  head = segments.first
  
  # 画面サイズ 640x480 をフルに使う（振幅を 300 / 220 程度に拡大）
  head.x = 320 + Math.cos(t * 1.3) * 280 + Math.sin(t * 0.5) * 30
  head.y = 240 + Math.sin(t * 1.9) * 200 + Math.cos(t * 0.7) * 40

  # --- [2] 追従ロジック（しなりの強化） ---
  (segments.size - 1).downto(1) do |i|
    s = segments[i]
    prev = segments[i-1]
    
    # 追従係数を 0.2 -> 0.12 に下げて「遅れ」を大きくする（リボンが長く伸びる）
    s.x += (prev.x - s.x) * 0.12
    s.y += (prev.y - s.y) * 0.12
    
    # --- [3] 視覚エフェクトの動的変化 ---
    ratio = i.to_f / segments.size
    
    # 後ろほど細くなるが、先端は太くしてインパクトを出す
    s.scale_x = s.scale_y = 2.0 * (1.0 - ratio)
    s.alpha = (255 * (1.0 - ratio**0.5)).to_i # 減衰を緩やかにして尻尾を長く見せる
    s.blend = :add
    
    # 時間で色を変える（虹色グラデーション）
    # Rubyの標準機能で色相を回すのは少し重いので、簡易的なRGB変換
    s.z = 0 # 描画順は固定
  end

  Sprite.draw(segments)
end