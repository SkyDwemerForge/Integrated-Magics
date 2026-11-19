# Feature 03 — Mais ícones/visuais de magia e customização por magia

**Tipo:** Feature · **Relatos:** 2

## Pedido
- Mais visuais embutidos além de fogo, gelo e raio.
- Permitir que o jogador importe os próprios ícones.
- Atribuir um visual único a cada magia individualmente.

## Relatos
> "I've figured out how to make some spells display lightning‑, frost‑ or fire‑themed visuals. It turns out this works automatically as long as a spell is tagged with the corresponding element attribute. Still, I'd like to know whether more built‑in spell visuals can be added, or if players can manually import their own ones. Also, is it possible to assign custom unique visuals to individual spells?"

> "When I bind a spell to a quickslot, a corresponding spell school icon is assigned, yet it appears plain white. I've seen screenshots [...] display red fire effects or yellow lightning visuals instead. I'd like to know how this visual change is achieved."

## Notas
- O segundo relato é só uma dúvida: a cor vem da tag de elemento da magia. Vale documentar isso na página do mod.

## Tarefas
- [ ] Documentar como os ícones por elemento são escolhidos.
- [ ] Carregar ícones extras de uma pasta (ex.: `SKSE/Plugins/IntegratedMagic/icons/`), com mapeamento por FormID/EditorID (JSON/INI).
- [ ] Adicionar mais ícones embutidos (outras escolas/elementos).

## Arquivos prováveis
- `TextureManager`, `src/UI/SlotDrawer.cpp`
