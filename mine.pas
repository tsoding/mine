program Mine;

uses Termio;

type
   Cell = (Empty, Bomb);
   Field = record
      Cells: array of Cell;
      Open: array of Boolean;
      Rows: Integer;
      Cols: Integer;
      CursorRow: Integer;
      CursorCol: Integer;
   end;

   function FieldGet(Field: Field; Row, Col: Integer): Cell;
   begin
      FieldGet := Field.Cells[Row*Field.Cols + Col];
   end;

   function FieldIsOpen(Field: Field; Row, Col: Integer): Boolean;
   begin
      FieldIsOpen := Field.Open[Row*Field.Cols + Col];
   end;

   procedure FieldOpenAtCursor(var Field: Field);
   begin
      Field.Open[Field.CursorRow*Field.Cols + Field.CursorCol] := True;
   end;

   function FieldCheckedGet(Field: Field; Row, Col: Integer; var Cell: Cell): Boolean;
   begin
      FieldCheckedGet := (0 <= Row) and (Row < Field.Rows) and (0 <= Col) and (Col < Field.Cols);
      if FieldCheckedGet then Cell := FieldGet(Field, Row, Col);
   end;

   procedure FieldSet(var Field: Field; Row, Col: Integer; Cell: Cell);
   begin
      Field.Cells[Row*Field.Cols + Col] := Cell;
   end;

   procedure FieldResize(var Field: Field; Rows, Cols: Integer);
   begin
      SetLength(Field.Cells, Rows*Cols);
      SetLength(Field.Open, Rows*Cols);
      Field.Rows := Rows;
      Field.Cols := Cols;
   end;

   function FieldRandomCell(Field: Field; var Row, Col: Integer): Cell;
   begin
      Row := Random(Field.Rows);
      Col := Random(Field.Cols);
      FieldRandomCell := FieldGet(Field, Row, Col);
   end;

   procedure FieldRandomize(var Field: Field; BombsPercentage: Integer);
   var
      Index, BombsCount: Integer;
      Row, Col: Integer;
   begin
      Field.CursorRow := 0;
      Field.CursorCol := 0;
      for Index := 0 to Field.Rows*Field.Cols do
      begin
         Field.Cells[Index] := Empty;
         Field.Open[Index] := False;
      end;
      if BombsPercentage > 100 then BombsPercentage := 100;
      BombsCount := (Field.Rows*Field.Cols*BombsPercentage + 99) div 100;
      for Index := 1 to BombsCount do
      begin
         while FieldRandomCell(Field, Row, Col) = Bomb do;
         FieldSet(Field, Row, Col, Bomb);
      end;
   end;

   function FieldCountNbors(Field: Field; Row, Col: Integer): Integer;
   var
      DRow, DCol: Integer;
      C: Cell;
   begin
      FieldCountNbors := 0;
      for DRow := -1 to 1 do
         for DCol := -1 to 1 do
            if (DRow <> 0) or (DCol <> 0) then
               if FieldCheckedGet(Field, Row + DRow, Col + DCol, C) then
                  if C = Bomb then
                     inc(FieldCountNbors);
   end;

   function FieldAtCursor(Field: Field; Row, Col: Integer): Boolean;
   begin
      FieldAtCursor := (Field.CursorRow = Row) and (Field.CursorCol = Col);
   end;

   procedure FieldWrite(Field: Field);
   var
      Row, Col, Nbors: Integer;
   begin
      for Row := 0 to Field.Rows-1 do
      begin
         for Col := 0 to Field.Cols-1 do
         begin
            if FieldAtCursor(Field, Row, Col) then Write('[') else Write(' ');
            if FieldIsOpen(Field, Row, Col) then
               case FieldGet(Field, Row, Col) of
                  Bomb: Write('*');
                  Empty: begin
                            Nbors := FieldCountNbors(Field, Row, Col);
                            if Nbors > 0 then Write(Nbors) else Write(' ');
                         end;
               end
            else Write('#');
            if FieldAtCursor(Field, Row, Col) then Write(']') else Write(' ');
         end;
         WriteLn
      end
   end;

const
   STDIN_FILENO = 0;

var
   MainField: Field;
   Quit: Boolean = False;
   TAttr: Termios;
   Cmd: Char;
begin
   Randomize;
   FieldResize(MainField, 10, 10);
   FieldRandomize(MainField, 20);

   if IsATTY(STDIN_FILENO) = 0 then
   begin
      WriteLn('ERROR: this is not a terminal!');
      Exit;
   end;
   TCGetAttr(STDIN_FILENO, TAttr);
   TAttr.c_lflag := TAttr.c_lflag and (not (ICANON or ECHO));
   TAttr.c_cc[VMIN] := 1;
   TAttr.c_cc[VTIME] := 0;
   TCSetAttr(STDIN_FILENO, TCSAFLUSH, &tattr);

   FieldWrite(MainField);

   while not Quit do
   begin
      Read(Cmd);
      case Cmd of
         'w': if MainField.CursorRow > 0                then dec(MainField.CursorRow);
         's': if MainField.CursorRow < MainField.Rows-1 then inc(MainField.CursorRow);
         'a': if MainField.CursorCol > 0                then dec(MainField.CursorCol);
         'd': if MainField.CursorCol < MainField.Cols-1 then inc(MainField.CursorCol);
         ' ': FieldOpenAtCursor(MainField);
      end;
      Write(Chr(27), '[', MainField.Rows,   'A');
      Write(Chr(27), '[', MainField.Cols*3, 'D');
      FieldWrite(MainField);
   end;
end.
