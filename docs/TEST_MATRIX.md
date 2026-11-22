# Integrated Magic — Matriz de Testes

Checklist de casos de uso para validar o funcionamento do sistema. Marque `[x]` ao validar.

> Dica: rode com build DEBUG e acompanhe `%LOCALAPPDATA%\Skyrim Special Edition\SKSE\IntegratedMagic.log`
> (prefixos `[FLOW]`, `[State]`, `[Input]`, `[SaveLoad]`).

---

## Dimensões do sistema (variáveis que afetam o comportamento)

| Dimensão | Valores possíveis |
|---|---|
| **Modo de ativação** | Hold, Press, Automatic |
| **Conteúdo do slot** | 1 spell (R), 1 spell (L), 2 spells iguais (dual), 2 spells diferentes, shout, power |
| **Tipo de spell** | Cast (fire-and-forget), Concentration, Bound, Power, Shout |
| **`autoAttack`** | on / off |
| **Perk de dual cast** | com / sem |
| **`skipEquipAnimationPatch`** | on / off |
| **`skipEquipAnimationOnReturnPatch`** | on / off |
| **`skipChannelingPatch`** | on / off |
| **`requireExclusiveHotkeyPatch`** | on / off |
| **`pressBothAtSamePatch`** | on / off |
| **Estado inicial das mãos** | embainhado (`wasHandsDown=true`) / já sacado (`wasHandsDown=false`) |
| **Equipamento prévio (restore)** | 2 armas, arma+escudo, 2 spells, arma+spell, mãos vazias, tocha |

---

## 0. Startup & hooks

Bloqueante: se algum item falhar, o resto da matriz não pode ser executado. Ver `specs/001-fix-trampoline-allocation/`.

- [ ] **0.1** Iniciar o jogo via SKSE → chega ao menu principal sem nenhum diálogo de erro de `IntegratedMagic.dll`
- [ ] **0.2** `IntegratedMagic.log` contém as seis linhas `[Hooks] ... installed` (PollInputDevicesHook, PlayerAnimGraphHook, D3DInitHook, DXGIPresentHook, MagicCasterInterruptHook, MagicCasterStartCastHook) e `[Hooks] hook space: 42 / 128 B used (33%), ...`, sem nenhuma linha `[Hooks]` de warn ou critical
- [ ] **0.3** Iniciar com o load order completo de plugins SKSE → mesmo resultado de 0.1 e 0.2
- [ ] **0.4** Repetir 0.1–0.3 no SE e no AE
- [x] **0.5** Primeira captura de hotkey no SKSE menu → `[Hooks] MenuInputShieldHook installed (outer, previous target in <módulo>)` uma única vez, sem `[Hooks]` de warn ou critical. `<módulo>` é o hook que estava por fora antes (no teste de validação, `InputManager.dll`, do mod Input Manager) (spec `specs/009-fix-capture-input-leak/`)
- [ ] **0.6** Build **release** (compilada por último), sessão com início, carregar save, abrir o SKSE menu e lançar um slot → toda linha `info` do `IntegratedMagic.log` é uma das 13 linhas sempre ativas de `specs/010-fix-debug-build-define/contracts/build-switch.md` §3; warnings e erros só se algo falhou; nenhuma linha `[Input]`, `[State]`, `[FLOW]`, `[SpellSystem]`, `[UI]`, `[SaveLoad]` ou `[Startup]` (spec `specs/010-fix-debug-build-define/`)
- [x] **0.7** Build **debug** (compilada por último), 3 inícios → `skse64.log` carrega `IntegratedMagic` sem erro; o log tem `[Startup] Install_Hooks (game module base=0x...)` com base diferente de 0, as seis linhas `[Hooks] ... installed`, a linha `hook space` e linhas de debug como `[Input]`, e nenhuma linha `[Startup] load fault:` (spec `specs/010-fix-debug-build-define/`)

---

## 1. Casos críticos de cast (regressão dos bugs recentes)

Os mais sensíveis — relacionados aos commits recentes (dual cast, skip channeling).

- [x] **1.1** Dual cast, mesma spell, COM perk, skipEquipAnim OFF, skipChanneling OFF, 2 armas equipadas → **um único cast dual** (anim de dual cast, custo de mana aumentado).
- [ ] **1.2** Dual cast, mesma spell, COM perk, skipEquipAnim **ON**, skipChanneling OFF → um único cast dual, sem anim de saque
- [x] **1.3** Dual cast, mesma spell, COM perk, skipChanneling **ON** → cast dual instantâneo (sem channeling)
- [ ] **1.4** Dual cast, mesma spell, **SEM perk** → comportamento definido/intencional (validar)
- [x] **1.5** Dual cast a partir de **mãos embainhadas** (`wasHandsDown=true`) → sobe as mãos + cast dual sincronizado
- [ ] **1.6** Dual cast a partir de **mãos já sacadas** (`wasHandsDown=false`) → cast dual sem re-saque

---

## 2. Matriz Modo × Conteúdo do slot

Baseline: **skipEquipAnim OFF / skipChanneling OFF**. Repetir depois com patches ON.

| Conteúdo | Hold | Press | Automatic |
|---|---|---|---|
| 1 spell direita | [ ] 2.1 | [ ] 2.2 | [ ] 2.3 |
| 1 spell esquerda | [ ] 2.4 | [ ] 2.5 | [ ] 2.6 |
| 2 spells iguais (dual) | [ ] 2.7 | [ ] 2.8 | [ ] 2.9 |
| 2 spells diferentes | [ ] 2.10 | [ ] 2.11 | [ ] 2.12 |

**Comportamentos esperados por modo:**
- **Hold**: segura tecla → loop de cast; solta → restaura equipamento anterior
- **Press**: 1ª pressão ativa; 2ª pressão (ou fim do cast) desativa e restaura
- **Automatic**: dispara uma vez, auto-sai ao completar o cast

---

## 3. Matriz Modo × Tipo de spell

| Tipo de spell | Hold | Press | Automatic | Notas |
|---|---|---|---|---|
| **Cast** (fire-and-forget, ex: Firebolt) | [ ] 3.1 | [ ] 3.2 | [ ] 3.3 | Charge → fire → restore |
| **Concentration** (ex: Flames, Healing) | [ ] 3.4 |
| **Bound** (ex: Bound Sword) | [ ] 3.7 | [ ] 3.8 | [ ] 3.9 | Cria arma — interação com restore |
| **Power** (greater/lesser power) | [ ] 3.10 | [ ] 3.11 | [ ] 3.12 | Slot de voz; cooldown diário |
| **Shout** | [ ] 3.13 | [ ] 3.14 | [ ] 3.15 | Slot de voz; 1-3 níveis de palavra |

---

## 4. `autoAttack` ON vs OFF

- [ ] **4.1** Spell com `autoAttack=true`, modo Hold → plugin injeta ataque sintético automaticamente
- [ ] **4.2** Spell com `autoAttack=false`, modo Hold → equipa a spell mas **não** dispara; jogador ataca manualmente
- [ ] **4.3** Dual cast com `autoAttack=false` em uma das mãos → validar consistência

---

## 5. Patches — combinações

| # | skipEquipAnim | skipOnReturn | skipChanneling | Cenário | OK |
|---|---|---|---|---|---|
| 5.1 | OFF | OFF | OFF | **Baseline** — tudo manual | [ ] |
| 5.2 | ON | OFF | OFF | Saque rápido, restore normal | [ ] |
| 5.3 | ON | ON | OFF | Saque e restore rápidos | [ ] |
| 5.4 | OFF | OFF | ON | Cast instantâneo (channeling pulado) | [ ] |
| 5.5 | ON | ON | ON | Tudo otimizado (config "ação rápida") | [ ] |
| 5.6 | OFF | ON | OFF | Apenas restore rápido (assimétrico) | [ ] |

> Repetir 5.1–5.5 para: 1 spell, dual cast, Concentration, Power/Shout.

### 5b. Skip Channeling × Bound weapons

Validar com o build **DEBUG** e acompanhar as linhas `[FLOW] SkipChanneling:` no log. Ver `specs/002-fix-skip-channeling-bound/`.

- [ ] **5b.1** Bound Sword, Automatic, mão direita, patch ON → invoca **uma vez**, sem pose de carga sustentada, a arma aparece na mão, sem loop
- [ ] **5b.2** Mesmo cenário na mão esquerda
- [ ] **5b.3** Bound Sword, Press + auto-attack, patch ON → mesmo resultado; a segunda pressão encerra e restaura como com o patch OFF
- [ ] **5b.4** Dual bound (mesma spell nas duas mãos) → log mostra um único `applied dual L+R`; as duas mãos invocam e encerram juntas
- [ ] **5b.5** Slot misto: Bound Sword na direita + Firebolt na esquerda → cada mão segue o seu tipo de spell, sem loop
- [ ] **5b.6** Bound Bow (duas mãos) → invoca uma vez e a sessão encerra normalmente
- [ ] **5b.7** Skip Equip Animations ON + Skip Channeling ON → sem loop (anotar se o saque da arma fica encurtado)
- [ ] **5b.8** Magicka gasta com o patch ON igual à do patch OFF (±1) para Bound Sword, Fireball e Firebolt
- [ ] **5b.9** Bound Sword já invocada (arma ainda ativa na mão) → ativar o slot de novo com o patch ON → sem loop; resultado igual ao do patch OFF
- [ ] **5b.10** Bloquear, sofrer stagger ou abrir um menu durante a invocação com o patch ON → a sessão termina, o equipamento é restaurado e não há loop (log mostra `ForceExit` ou `SkipChanneling: interrupted after skip`)
- [ ] **5b.11** Magia de concentração (Flames) em Automatic com o patch ON → carga e canalização normais; log mostra `SkipChanneling: skipped ... reason=type=Concentration`

---

## 6. Patches de input

- [x] **6.1** `requireExclusiveHotkey` ON — combo R1+L1; pressionar R1+L1+R2 → slot **não** ativa
- [x] **6.2** `requireExclusiveHotkey` ON — pressionar exatamente R1+L1 → slot ativa
- [x] **6.3** `pressBothAtSame` ON — segurar R1 por 2s, depois pressionar L1 → slot **não** ativa
- [x] **6.4** `pressBothAtSame` ON — pressionar R1+L1 dentro da janela → slot ativa
- [x] **6.5** Ambos ON → validar interação
- [x] **6.6** Replay system — tecla segurada atravessando a janela de timing → evento não é perdido
- [x] **6.7** Teclado vs Gamepad — mesmo slot com combos de KB e GP → ambos funcionam

Roda do mouse × hotkeys (spec `specs/003-fix-mouse-wheel-hotkeys/`):

- [x] **6.8** `requireExclusiveHotkey` ON — zoom da câmera com a roda → hotkey do slot ativa no 1º toque
- [x] **6.9** `requireExclusiveHotkey` ON — scroll no QuickLoot IE (>1 item) → hotkey ativa
- [x] **6.10** `requireExclusiveHotkey` ON — vários scrolls nos dois sentidos + alternar 1ª/3ª pessoa → todos os
  slots ativam, sem abrir menu
- [x] **6.11** `requireExclusiveHotkey` ON — segurar slot Hold e rolar a roda → o cast continua até soltar a tecla.
  Rolar entre e durante o aperto de `Shift + 5` → o slot ativa
- [x] **6.12** Captura de hotkey — rolar a roda durante a captura → a roda não é gravada
- [x] **6.13** `requireExclusiveHotkey` OFF — passos do 1º relato (zoom, alternar 1ª/3ª pessoa, teclas 1–4) → 10/10
  ativam. Falha = segunda causa, abrir spec separada
- [x] **6.14** `requireExclusiveHotkey` ON — segurar tecla fora do combo (ex.: `G`) e apertar o slot → não ativa, e o
  log mostra `REFUSED (not exclusive) extra=kb:0x22` uma vez

Gamepad × slots compartilhando botões (spec `specs/004-fix-gamepad-slot-priority/`):

- [x] **6.15** 8 slots, slots 5–8 = `face + RT + LB`. Segurar `RT + LB` e apertar o botão de face por último → o slot
  5–8 conjura, sem pulo/ataque/menu do jogo
- [x] **6.16** Ordem (slots 1–8, acorde rápido): botão de face um pouco antes do `RT` (ex.: Triângulo/`Y` antes do
  R2/`RT`) e também `RT` primeiro → conjura nas duas ordens, sem pulo/ação do botão de face e sem ataque da mão
  direita
- [x] **6.17** Botões sozinhos (`RT`, `Y`, `A`, `B`) sem completar combo → a ação normal do jogo acontece, sem atraso
  perceptível
- [x] **6.18** Sobreposição: segurar `RT + LB`, `Y` por último → o slot 6 dispara e o slot 2 não; o log mostra
  `SUPPRESSED by slot=5`
- [x] **6.19** Combos idênticos (slot 8 = `B + RT` como o slot 3) → aviso `HotkeyCache: slots 2 and 7 have the same gp
  combo` ao carregar; o slot 3 dispara
- [x] **6.20** `requireExclusiveHotkey` ON + sobreposição → o slot 6 dispara e o slot 2 é recusado com `extra=gp:8`
- [x] **6.21** Teclado: slot 1 `Shift + 1`, slot 5 `Shift + 5` → cada combo ativa o próprio slot, e `Shift` não chega ao
  jogo
- [x] **6.22** Modo Hold: segurar o combo do slot 6 (`Y + RT + LB`) por ~2 s → nenhuma ação do jogo durante o hold; ao
  soltar, nenhum botão fica preso
- [x] **6.23** Slot 2 em Press sem auto-ataque ativo: apertar `RT` sozinho → conjura manualmente; o log mostra
  `handed to game` para `RT`

---

## 7. Snapshot / Restore (equipamento prévio)

- [ ] **7.1** 2 armas (R+L) → ambas restauradas nas mãos corretas
- [ ] **7.2** Arma + escudo → arma e escudo restaurados
- [ ] **7.3** 2 spells (R+L) → spells restauradas
- [ ] **7.4** Arma direita + spell esquerda → cada uma na mão certa
- [ ] **7.5** Mãos vazias → permanecem vazias
- [ ] **7.6** Arma de duas mãos → restaurada corretamente
- [ ] **7.7** Tocha na esquerda + arma direita → tocha e arma restauradas
- [ ] **7.8** Shout/power já equipado na voz → voz restaurada
- [ ] **7.9** Itens "extra equipped" (overlay) → `prevExtraEquipped` restaurado
- [ ] **7.10** Restore após sheathe (`pendingRestoreAfterSheathe`) → aguarda anim de embainhar antes de restaurar

Armas sacadas com arma/escudo na mão esquerda (spec `specs/007-fix-drawn-weapon-cast/`). Rodar com Skip Equip
Animation e Skip Equip Animation on Return desligados e depois ligados. Slots em Automatic salvo indicação:

- [ ] **7.11** Duas espadas diferentes, armas sacadas: slot de mão esquerda, de mão direita e dual, 10× seguidas cada →
  todas conjuram; as duas espadas voltam às mãos certas, ainda sacadas
- [ ] **7.12** Duas cópias da mesma espada, armas sacadas: mesmo teste do 7.11 → todas conjuram; as duas espadas voltam
  e nenhuma mão fica vazia
- [ ] **7.13** Espada + escudo, armas sacadas: mesmo teste do 7.11 → todas conjuram; espada e escudo voltam, ainda
  sacados
- [ ] **7.14** Sequência do relato (7.11 e 7.13): embainhar → conjurar → sacar → conjurar 5× → as 6 conjurações
  funcionam, não só a primeira depois de sacar
- [ ] **7.15** Logo depois de o equipamento voltar (espada + escudo): um ataque e uma defesa funcionam no primeiro
  aperto, sem embainhar
- [ ] **7.16** Hold e Press com auto-ataque, duas espadas e espada + escudo, armas sacadas, 5 usos cada → conjuram como
  com magia nas duas mãos
- [x] **7.17** Skip Equip Animation desligado, arma na mão esquerda: sacar e ativar o slot de mão esquerda **durante a
  animação de sacar**, 5× → conjura quando a animação termina (cerca de 1,5 s), sem cancelar. O log mostra
  `EquipAnimGrace: ... retry bound paused` e não mostra `cast did not start`

---

## 8. Interrupções e saídas forçadas

- [x] **8.1** `blockStart` / `BashExit` (não-Press) → ForceExit + restore
- [ ] **8.2** `staggerStart` / `KnockDown` → ForceExit + restore
- [ ] **8.3** Morte do jogador → ForceExit
- [ ] **8.4** Abrir menu (Inventory, Magic, Map, etc.) → ForceExit
- [ ] **8.5** Load game durante sessão ativa → ForceExit + reset de input
- [ ] **8.6** Equip estrangeiro (outro mod equipa algo) → ForceExitNoRestore
- [ ] **8.7** Timeout (>30s travado) → ForceExit automático
- [ ] **8.8** Trocar de slot enquanto um está ativo → PrepareForOverwrite + ativa o novo

Botão de ataque/defesa × slots (spec `specs/005-fix-left-hand-block/`). Testar em teclado/mouse (botão direito) e
gamepad (`LT`). `requireExclusiveHotkey` OFF, salvo indicação:

- [x] **8.9** Defesa segurada (escudo, e depois arma de uma mão na esquerda) → ativar slot de mão esquerda
  (Hold/Automatic) → a defesa baixa, a magia sai, sem tremer; log `button taken at cast start`
- [x] **8.10** Defesa segurada → ativar slot de mão direita → o escudo baixa e a magia sai. Com
  `requireExclusiveHotkey` ON → slot recusado, log `REFUSED (not exclusive) extra=mouse:1` (ou `gp:`)
- [x] **8.11** Slot de mão esquerda em Hold/Automatic: apertar/segurar a defesa durante o cast → a sessão cancela, o
  equipamento volta; só defende ao reapertar. Log `press during plugin cast -> cancel session`
- [x] **8.12** Slot de mão esquerda em Press com auto-ataque: apertar a defesa → sem efeito, a magia sai. Log
  `ignored (Press mode)`
- [x] **8.13** Cliques rápidos na defesa durante 8.11 e 8.12 → sem tremer, mesmo resultado
- [x] **8.14** Slot de mão direita com escudo: defesa durante Hold/Automatic → o slot encerra na hora e o personagem
  defende sem reapertar (log `other-hand press -> passes, cancel session`); em Press o aperto não cancela
- [x] **8.15** Dual cast com 8.9 e 8.11 → um único dual cast ou cancelamento limpo, nunca dois casts simples
- [x] **8.16** Sem estado preso: depois de 8.9–8.14, 10 ativações sem tocar na defesa conjuram de primeira; botão
  tomado e ainda segurado depois do fim da sessão não faz nada até ser reapertado
- [x] **8.17** Cast interrompido pelo jogo nunca deixa o slot ativo (nenhum Automatic fica ativo por 30 s). Log
  `real interrupt -> finish + finalize session`
- [x] **8.18** Ativar slot no início de um pulo → conjura ao pousar, ou a sessão termina em ~1 s com o equipamento de
  volta. Log `cast did not start in ... -> ForceExit`
- [x] **8.19** Sem sessão: defesa, bash e cast manual da mão esquerda como no vanilla (sem linhas `HandGate`); cast
  manual em Press sem auto-ataque continua funcionando

---

## 9. Powers / Shouts (slot de voz)

- [ ] **9.1** Power em modo Hold → dispara, aplica cooldown diário
- [ ] **9.2** Power em modo Automatic → dispara após `kPowerAutoDuration` (0.2s)
- [ ] **9.3** Shout 1 palavra vs 3 palavras → nível correto disparado
- [ ] **9.4** Cooldown de power exibido no HUD → ring de cooldown progride corretamente
- [ ] **9.5** Power em cooldown — tentar reativar → não dispara / feedback
- [ ] **9.6** Shout interrompido (`shoutStop`) → finaliza e restaura

---

## 10. HUD

- [ ] **10.1** Abrir popup de atribuição (toggle + Magic Menu) → popup renderiza
- [ ] **10.2** Clicar em slot para atribuir spell hovered → `MagicAssign::ComputeSpellAssignment` aplica
- [ ] **10.3** Atribuir shout/power a slot de voz → atribuição correta
- [ ] **10.4** Slot ativo recebe glow → visual correto
- [ ] **10.5** Cooldown arc em spell/power → progresso correto
- [ ] **10.6** Ícones de spell carregados (TextureManager) → exibidos
- [x] **10.7** Rebind de hotkey (CaptureState) → captura próxima tecla
- [ ] **10.8** Labels de KB e GP nos slots → corretos
- [x] **10.9** Captura de hotkey repetida (20× seguidas, teclado e gamepad, concluídas e canceladas com X) → o menu
  do SKSE continua totalmente opaco; a página de outro mod no menu do SKSE fica normal (spec
  `specs/006-fix-menu-capture-transparency/`)
- [x] **10.10** "- Vertex", "Apply changes", botão "M", grade de âncora do HUD e cantos do rótulo, 10× cada → o menu
  nunca desbota e cada controle mantém a aparência

Magic Menu: atalhos de slot e da wheel sem acionar funções do menu (spec `specs/008-fix-magic-menu-hotkey-leak/`):

- [x] **10.11** Spell em hover, hotkey de teclado de uma tecla e de duas teclas (as duas em até 0,3 s), 10× cada → a
  spell vai para o slot e o menu não reage a nenhuma das teclas; log `MenuHold: ... consumed by slot=`
- [x] **10.12** Gamepad, layout padrão (`RT` + botão), 10 atribuições, metade com `RT` primeiro e metade com o botão
  primeiro → atribui e nenhum dos dois botões aciona função do menu; shout/power em hover vai para a voz do slot
- [x] **10.13** Hotkey da wheel no Magic Menu, teclado e gamepad, abrir e fechar 10× cada → a wheel alterna e o menu
  não reage; log `consumed by wheel`
- [ ] **10.14** Botão de hotkey sozinho no Magic Menu (ex.: `RT` sozinho, `Shift` sozinho): toque rápido 10× → o menu
  executa a ação normal uma vez, na soltura; segurado ~1 s → reage em até 0,3 s e continua enquanto segurado
- [ ] **10.15** Botões fora de qualquer hotkey no Magic Menu (navegação, categorias) → sem atraso
- [ ] **10.16** Fechar o Magic Menu com uma tecla de hotkey segurada, e fechar até 0,3 s depois de apertar uma, 5× cada
  → de volta ao jogo, movimento, ataque e slots funcionam no primeiro aperto
- [x] **10.17** Hotkeys sobrepostas (ex.: `RT + LT + A` e `RT + A`): atribuir com a mais longa 10×, em ordens
  diferentes → só o slot dela recebe a spell (log `QuickBind: ... SUPPRESSED by slot=`); atribuir com a mais curta
  10× → só o slot dela recebe, em até 0,3 s ou na soltura

Captura de hotkey no SKSE menu sem o menu reagir ao input capturado (spec `specs/009-fix-capture-input-leak/`):

- [ ] **10.18** Campo de gamepad: capturar cada um dos 16 botões/gatilhos (D-pad, Start, Back, L3, R3, LB, RB, A, B,
  X, Y, LT, RT) → cada valor gravado; o menu não move o foco, não ativa nada, não volta, não fecha, não abre o seletor
  de janelas; D-pad segurado não move o foco até soltar
- [ ] **10.19** Campo de teclado: capturar setas, `Enter`, `Space`, `Tab` e os botões esquerdo, direito e do meio do
  mouse sobre outro controle → valor gravado e nenhum controle reage
- [ ] **10.20** Ativar "Cap" com `A` do gamepad, e com `Enter`/`Space` no teclado → esse mesmo botão/tecla não é
  capturado; a captura continua esperando
- [ ] **10.21** Outro dispositivo durante a captura (gamepad num campo de teclado, tecla num campo de gamepad), 5× cada
  → captura cancelada, campo sem mudança, menu sem reação; `Escape` cancela a captura e o SKSE menu continua aberto
  (um segundo `Escape`, depois de soltar, fecha o menu)
- [ ] **10.22** Depois de cada captura, ao soltar tudo, o menu reage normalmente no primeiro aperto; fechar o SKSE menu
  durante uma captura e reabrir → nada capturado e navegação normal

---

## 11. Persistência

- [ ] **11.1** Atribuir spells, salvar, recarregar → slots preservados (SaveSpellDB)
- [ ] **11.2** Mudar modo de ativação de uma spell, trocar de save → setting global preservado (SpellSettingsDB)
- [ ] **11.3** Save A vs Save B com slots diferentes → cada save mantém seus próprios slots
- [ ] **11.4** Atribuição alterada → escrita imediata → persiste sem precisar salvar manualmente

---

## Prioridade de execução sugerida

0. **Seção 0** (startup & hooks) — bloqueante
1. **Seção 1** (regressão dual cast + channeling) — risco mais alto, bugs recentes
2. **Seções 2 e 3** (matriz modo × conteúdo × tipo) — cobertura funcional central
3. **Seções 5 e 6** (patches) — interações conhecidas por causar bugs espúrios
4. **Seções 7 e 8** (restore + interrupções) — correção de estado/equipamento
5. **Seções 9–11** — features complementares
