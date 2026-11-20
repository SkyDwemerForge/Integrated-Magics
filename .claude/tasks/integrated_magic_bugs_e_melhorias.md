# Integrated Magic --- Bugs, Compatibilidade e Melhorias

Documento consolidado a partir dos relatos dos usuários e das tarefas
identificadas.

## 1. Bugs identificados

### BUG-01 --- Teclas de atalho param de funcionar após utilizar o scroll do mouse

**Descrição:** Após movimentar a roda do mouse, as teclas de atalho
configuradas para conjurar magias deixam de funcionar.

**Comportamentos observados:** - Acontece ao alternar entre primeira e
terceira pessoa. - Também acontece ao utilizar o scroll para navegar por
itens no QuickLoot IE. - O problema pode ocorrer mesmo quando a roda do
mouse não está configurada para alterar a câmera. - As teclas voltam a
funcionar após abrir um menu que pausa o jogo, como inventário, magia ou
favoritos. - No QuickLoot IE, o problema foi observado ao utilizar o
scroll do mouse, mas não ao navegar com as teclas direcionais.

**Condição relacionada:** Foi identificada uma interação com a opção
`Require exclusive hotkey`, localizada na seção `Patches`. Quando
ativada, ela provoca o comportamento ao utilizar o scroll do QuickLoot
IE.

**Hipótese sobre a causa:** O estado de entrada do scroll pode estar
permanecendo como "pressionado", fazendo com que o sistema de hotkeys
não processe corretamente as entradas seguintes. Essa hipótese ainda
precisa ser confirmada.

### BUG-02 --- Conflito de prioridade dos comandos em gamepads nos slots acima do quarto

**Descrição:** Ao utilizar um controle, as combinações de botões
configuradas nos slots 5 ou superiores perdem prioridade para os
comandos padrão do jogo.

**Exemplo:** - Slot 1: RT + B → conjura a magia normalmente. - Slot 5:
RT + B → executa o comando padrão de pular, em vez de conjurar a magia.

**Comportamento esperado:** Todos os slots devem ter a mesma prioridade
de entrada, independentemente de sua posição.

### BUG-03 --- Interrupção e oscilação da conjuração com a mão esquerda

**Descrição:** A conjuração com a mão esquerda pode entrar em um estado
instável, no qual a animação fica alternando ou sendo interrompida
repetidamente.

**Comportamentos observados:** - O problema está relacionado ao
comportamento de bloqueio (`Block`), que utiliza o botão direito do
mouse. - Segurar a tecla de conjuração enquanto pressiona repetidamente
o botão de bloqueio pode corrigir temporariamente o problema. - Entrar
no estado de bloqueio enquanto mantém a conjuração pressionada pode
provocar o problema.

**Possível origem:** Conflito entre os estados de bloqueio e conjuração
da mão esquerda.

### BUG-04 --- Combinação de botões no gamepad exige dois pressionamentos para conjurar

**Descrição:** Ao utilizar um gamepad, pressionar uma combinação de
botões configurada para uma magia apenas seleciona ou equipa a magia,
sem conjurá-la.

É necessário pressionar a mesma combinação novamente para efetivamente
conjurar.

**Comportamento esperado:** Um único pressionamento da combinação deve
executar a ação configurada, conforme o modo de ativação selecionado.

### BUG-05 --- Modo Automatic pode executar a sequência de conjuração sem lançar a magia

**Descrição:** Ocasionalmente, ao utilizar o modo `Automatic`, a
conjuração com a mão esquerda executa toda a sequência de equipar,
conjurar e desequipar, mas nenhuma magia é efetivamente lançada.

**Observações:** - O problema acontece esporadicamente. - O usuário não
conseguiu identificar uma sequência consistente para reproduzi-lo. -
Configurar modos específicos (`Press`, `Hold` etc.) para cada tipo de
magia aparentemente evita o problema.

### BUG-06 --- Crash ao pressionar a tecla H

**Descrição:** O jogo apresenta um crash sempre que o usuário pressiona
a tecla `H` com o mod instalado.

**Observação:** Não foram fornecidos detalhes sobre conflitos com outros
mods, configurações de hotkeys ou condições adicionais para reproduzir o
problema.

### BUG-07 --- Falha na conjuração após desembainhar armas

**Descrição:** Na versão 1.5.1, foi identificado um problema de
conjuração ao utilizar determinadas combinações de armas.

**Condições em que o problema ocorre:** - Duas espadas equipadas. -
Escudo na mão esquerda e espada na mão direita.

**Comportamento observado:** 1. Com as armas embainhadas, as magias são
conjuradas normalmente. 2. Após desembainhar as armas, a conjuração
geralmente falha. 3. Se o jogador embainhar as armas, conjurar uma magia
com sucesso e desembainhá-las novamente, a próxima conjuração funciona.
4. Todas as conjurações seguintes voltam a falhar consistentemente.

**Combinações que não apresentam o problema:** - Magias equipadas nas
duas mãos. - Magia na mão esquerda e espada na mão direita.

O problema parece estar relacionado à interação entre o sistema de
conjuração e os estados de armas desembainhadas.

## 2. Compatibilidade

### COMPAT-01 --- Patch para Magic Casting Behavior Overhaul

**Descrição:** Necessidade de implementar ou corrigir um patch de
compatibilidade para o mod `Magic Casting Behavior Overhaul`.

**Observação:** O relato original não especifica qual comportamento
apresenta incompatibilidade.

## 3. Problemas de comportamento e funcionalidades

### FUNC-01 --- Magias de concentração não devem aplicar Activation Lock

**Descrição:** Magias de concentração não devem aplicar o bloqueio de
ativação (`Activation Lock`).

O mecanismo precisa considerar o tipo de magia para evitar bloquear
indevidamente a ativação de magias que exigem conjuração contínua.

### FUNC-02 --- Quick Bound não deve ativar comandos no Magic Menu

**Descrição:** O comando `Quick Bound` não deve executar suas ações
quando o jogador estiver utilizando o Magic Menu.

É necessário impedir que a hotkey interfira nas operações realizadas
dentro desse menu.

## 4. Melhorias visuais

### UI-01 --- Aumentar o tamanho da fonte dos títulos das magias

**Descrição:** Aumentar o tamanho da fonte utilizada para exibir os
nomes das magias, melhorando a legibilidade.

### UI-02 --- Ícones das escolas de magia aparecem completamente brancos

**Descrição:** Ao vincular uma magia a um Quickslot, o ícone
correspondente à escola de magia é atribuído, mas aparece completamente
branco.

O usuário observou imagens de outros jogadores nas quais os ícones
apresentam efeitos visuais coloridos, como fogo vermelho e raios
amarelos.

É necessário investigar por que esses efeitos visuais não aparecem e se
dependem de configurações, assets ou implementação específica.

## 5. Tarefas técnicas e refatoração

### TECH-01 --- Remover `Slots.h` de `SpellSystemController`

**Descrição:** Eliminar a dependência ou inclusão de `Slots.h` no
`SpellSystemController`.

## 6. Resumo

  Categoria              Quantidade
  -------------------- ------------
  Bugs identificados              7
  Compatibilidade                 1
  Funcionalidades                 2
  Melhorias visuais               2
  Tarefas técnicas                1
  **Total**                  **13**

## 7. Observações sobre a consolidação

-   O problema com `Require exclusive hotkey` e QuickLoot IE foi
    incorporado ao BUG-01, pois aparenta ser uma manifestação do mesmo
    problema com o estado do scroll. Não deve ser tratado como um bug
    separado.
-   A incompatibilidade com versões recentes de `Skip Equip Animation`
    foi removida da lista: o problema era causado pelo uso de uma versão
    antiga do próprio Integrated Magic e já foi resolvido.
-   O patch para `Magic Casting Behavior Overhaul` foi mantido como uma
    única tarefa de compatibilidade, sem duplicá-lo na seção de tarefas
    técnicas.