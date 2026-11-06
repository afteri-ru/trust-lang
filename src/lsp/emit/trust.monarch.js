    {
      defaultToken: '',
        tokenizer: {
          root: [
            [/\/\*/, 'comment.block', '@comment'],
            [/#.*$/, 'comment.line'],
            [/@\w[\w_]*/, 'keyword.macro'],
            [/@[{}[\]<>:|]/, 'keyword.control'],
            [/\$\$|\$\*|\$\.\.\.|\$\^/, 'variable.language'],
            [/\$\d+/, 'variable.language'],
            [/\$[A-Za-z_]\w*/, 'variable'],
            [/:[A-Za-z_]\w*/, 'type'],
            [/%[A-Za-z_]\w*/, 'keyword.function'],
            [/R"[\s\S]*?"/, 'string.raw'],
            [/R'[\s\S]*?'/, 'string.raw'],
            [/`[^`]*`/, 'string'],
            [/"/, 'string.double', '@str_d'],
            [/'/, 'string.single', '@str_s'],
            [/-?\d[\d_]*\.\d+([eE][-+]?\d+)?|-?\d[\d_]*([eE][-+]?\d+)?/, 'number'],
            [/[a-zA-Z_]\w*/, 'identifier'],
            [/[{}()\[\]]/, '@brackets'],
            [/[=:;,.+\-*\/%&|^~!<>]+/, 'operator'],
            [/\s+/, 'white']
          ],
          comment: [
            [/[^/*]+/, 'comment.block'],
            [/\/\*/, 'comment.block', '@push'],
            [/\*\//, 'comment.block', '@pop'],
            [/[/*]/, 'comment.block']
          ],
          str_d: [
            [/[^"\\]+/, 'string.double'],
            [/\\./, 'string.escape'],
            [/"/, 'string.double', '@pop']
          ],
          str_s: [
            [/[^'\\]+/, 'string.single'],
            [/\\./, 'string.escape'],
            [/'/, 'string.single', '@pop']
          ]
        }
    }
