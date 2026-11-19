# Feature 04 — Tamanho de fonte configurável para os nomes das magias

**Tipo:** Feature · **Relatos:** 1

## Pedido
Permitir aumentar o tamanho da fonte dos nomes das magias exibidos no HUD.

## Relato
> "Is there a way to increase the font size when you display the spell titles?"

## Tarefas
- [ ] Adicionar uma opção de escala/tamanho da fonte em `HudSettings`.
- [ ] Aplicar no desenho do nome no `SlotDrawer` (via `ImGui::SetWindowFontScale` ou uma fonte carregada em tamanho maior, para não perder nitidez).
- [ ] Expor na UI de configuração e persistir em `ConfigAdapter`.

## Arquivos prováveis
- `include/Config/Ports/HudSettings.h`, `src/Config/ConfigAdapter.cpp`
- `src/UI/SlotDrawer.cpp`
