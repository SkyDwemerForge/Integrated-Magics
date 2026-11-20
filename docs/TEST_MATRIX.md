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

---

## 1. Casos críticos de cast (regressão dos bugs recentes)

Os mais sensíveis — relacionados aos commits recentes (dual cast, skip channeling).

- [ ] **1.1** Dual cast, mesma spell, COM perk, skipEquipAnim OFF, skipChanneling OFF, 2 armas equipadas → **um único cast dual** (anim de dual cast, custo de mana aumentado).
- [ ] **1.2** Dual cast, mesma spell, COM perk, skipEquipAnim **ON**, skipChanneling OFF → um único cast dual, sem anim de saque
- [ ] **1.3** Dual cast, mesma spell, COM perk, skipChanneling **ON** → cast dual instantâneo (sem channeling)
- [ ] **1.4** Dual cast, mesma spell, **SEM perk** → comportamento definido/intencional (validar)
- [ ] **1.5** Dual cast a partir de **mãos embainhadas** (`wasHandsDown=true`) → sobe as mãos + cast dual sincronizado
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

- [ ] **6.1** `requireExclusiveHotkey` ON — combo R1+L1; pressionar R1+L1+R2 → slot **não** ativa
- [ ] **6.2** `requireExclusiveHotkey` ON — pressionar exatamente R1+L1 → slot ativa
- [ ] **6.3** `pressBothAtSame` ON — segurar R1 por 2s, depois pressionar L1 → slot **não** ativa
- [ ] **6.4** `pressBothAtSame` ON — pressionar R1+L1 dentro da janela → slot ativa
- [ ] **6.5** Ambos ON → validar interação
- [ ] **6.6** Replay system — tecla segurada atravessando a janela de timing → evento não é perdido
- [ ] **6.7** Teclado vs Gamepad — mesmo slot com combos de KB e GP → ambos funcionam

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

---

## 8. Interrupções e saídas forçadas

- [ ] **8.1** `blockStart` / `BashExit` (não-Press) → ForceExit + restore
- [ ] **8.2** `staggerStart` / `KnockDown` → ForceExit + restore
- [ ] **8.3** Morte do jogador → ForceExit
- [ ] **8.4** Abrir menu (Inventory, Magic, Map, etc.) → ForceExit
- [ ] **8.5** Load game durante sessão ativa → ForceExit + reset de input
- [ ] **8.6** Equip estrangeiro (outro mod equipa algo) → ForceExitNoRestore
- [ ] **8.7** Timeout (>30s travado) → ForceExit automático
- [ ] **8.8** Trocar de slot enquanto um está ativo → PrepareForOverwrite + ativa o novo

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
- [ ] **10.7** Rebind de hotkey (CaptureState) → captura próxima tecla
- [ ] **10.8** Labels de KB e GP nos slots → corretos

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
