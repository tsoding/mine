{ -*- mode: opascal -*- }
program Mine;

uses Termio;

type
   Cell = (Empty, Bomb);
   State = (Closed, Open, Flagged);
   Field = record
      Generated: Boolean;
      Cells: array of array of Cell;
      States: array of array of State;
      Rows: Integer;
      Cols: Integer;
      CursorRow: Integer;
      CursorCol: Integer;
   end;

   procedure FlagAtCursor(var Field: Field);
   begin
      with Field do
         case States[CursorRow][CursorCol] of
            Closed:  States[CursorRow][CursorCol] := Flagged;
            Flagged: States[CursorRow][CursorCol] := Closed;
         end
   end;

   procedure RandomCell(Field: Field; var Row, Col: Integer);
   begin
      Row := Random(Field.Rows);
      Col := Random(Field.Cols);
   end;

   function IsAroundCursor(Field: Field; Row, Col: Integer): Boolean;
   var
      DRow, DCol: Integer;
   begin
      for DRow := -1 to 1 do
         for DCol := -1 to 1 do
            if (Field.CursorRow + DRow = Row) and (Field.CursorCol + DCol = Col) then
               Exit(True);
      IsAroundCursor := False;
   end;

   procedure FieldRandomize(var Field: Field; BombsPercentage: Integer);
   var
      Index, BombsCount: Integer;
      Row, Col: Integer;
   begin
      with Field do
      begin
         for Row := 0 to Rows - 1 do
            for Col := 0 to Cols - 1 do
               Cells[Row][Col] := Empty;
         if BombsPercentage > 100 then BombsPercentage := 100;
         BombsCount := (Rows*Cols*BombsPercentage + 99) div 100;
         for Index := 1 to BombsCount do
         begin
            { TODO: prevent this loop going indefinitely }
            repeat
               RandomCell(Field, Row, Col)
            until (Cells[Row][Col] <> Bomb) and not IsAroundCursor(Field, Row, Col);
            Cells[Row][Col] := Bomb;
         end;
      end;
   end;

   function FieldContains(Field: Field; Row, Col: Integer): Boolean;
   begin
      FieldContains := (0 <= Row) and (Row < Field.Rows) and (0 <= Col) and (Col < Field.Cols);
   end;

   function CountNborBombs(Field: Field; Row, Col: Integer): Integer;
   var
      DRow, DCol: Integer;
   begin
      CountNborBombs := 0;
      with Field do
         for DRow := -1 to 1 do
            for DCol := -1 to 1 do
               if (DRow <> 0) or (DCol <> 0) then
                  if FieldContains(Field, Row + DRow, Col + DCol) then
                     if Cells[Row + DRow][Col + DCol] = Bomb then
                        Inc(CountNborBombs);
   end;

   function CountNborFlags(Field: Field; Row, Col: Integer): Integer;
   var
      DRow, DCol: Integer;
   begin
      CountNborFlags := 0;
      with Field do
         for DRow := -1 to 1 do
            for DCol := -1 to 1 do
               if (DRow <> 0) or (DCol <> 0) then
                  if FieldContains(Field, Row + DRow, Col + DCol) then
                     if States[Row + DRow][Col + DCol] = Flagged then
                        Inc(CountNborFlags);
   end;

   function OpenAt(var Field: Field; Row, Col: Integer): Boolean;
   var
      DRow, DCol: Integer;
   begin
      with Field do
      begin
         if not Generated then
         begin
            FieldRandomize(Field, 20);
            Generated := True;
         end;

         States[Row][Col] := Open;

         if CountNborBombs(Field, Row, Col) = CountNborFlags(Field, Row, Col) then
            for DRow := -1 to 1 do
               for DCol := -1 to 1 do
                  if FieldContains(Field, DRow + Row, DCol + Col) then
                     if States[DRow + Row][DCol + Col] = Closed then
                        if OpenAt(Field, DRow + Row, DCol + Col) then
                           Exit(True);

         OpenAt := Cells[Row][Col] = Bomb;
      end
   end;

   function OpenAtCursor(var Field: Field): Boolean;
   begin
      OpenAtCursor := OpenAt(Field, Field.CursorRow, Field.CursorCol);
   end;

   {TODO: Open only unflagged bomb. Also indicate false flags somehow}
   procedure OpenAllBombs(var Field: Field);
   var
      Row, Col: Integer;
   begin
      with Field do
         for Row := 0 to Rows - 1 do
            for Col := 0 to Cols - 1 do
               if Cells[Row][Col] = Bomb then
                  States[Row][Col] := Open;
   end;

   procedure FieldReset(var Field: Field; Rows, Cols: Integer);
   var
      Row, Col: Integer;
   begin
      Field.Generated := False;
      Field.CursorRow := 0;
      Field.CursorCol := 0;
      SetLength(Field.Cells, Rows, Cols);
      SetLength(Field.States, Rows, Cols);
      Field.Rows := Rows;
      Field.Cols := Cols;
      for Row := 0 to Rows - 1 do
          for Col := 0 to Cols - 1 do
             Field.States[Row][Col] := Closed;
   end;

   function IsAtCursor(Field: Field; Row, Col: Integer): Boolean;
   begin
      IsAtCursor := (Field.CursorRow = Row) and (Field.CursorCol = Col);
   end;

   procedure FieldDisplay(Field: Field);
   var
      Row, Col, Nbors: Integer;
   begin
      with Field do
         for Row := 0 to Rows-1 do
         begin
            for Col := 0 to Cols-1 do
            begin
               if IsAtCursor(Field, Row, Col) then Write('[') else Write(' ');
               case States[Row][Col] of
                  Open: case Cells[Row][Col] of
                           Bomb: Write('@');
                           Empty: begin
                                     Nbors := CountNborBombs(Field, Row, Col);
                                     if Nbors > 0 then Write(Nbors) else Write(' ');
                                  end;
                        end;
                  Closed: Write('.');
                  {TODO: flag does not stand out enough}
                  Flagged: Write('P');
               end;
               if IsAtCursor(Field, Row, Col) then Write(']') else Write(' ');
            end;
            WriteLn
         end
   end;

   procedure MoveUp(var Field: Field);
   begin
      with Field do if CursorRow > 0 then Dec(CursorRow);
   end;

   procedure MoveDown(var Field: Field);
   begin
      with Field do if CursorRow < Rows-1 then Inc(CursorRow);
   end;

   procedure MoveLeft(var Field: Field);
   begin
      with Field do if CursorCol > 0 then Dec(CursorCol);
   end;

   procedure MoveRight(var Field: Field);
   begin
      with Field do if CursorCol < Cols-1 then Inc(CursorCol);
   end;

const
   STDIN_FILENO = 0;

var
   MainField: Field;
   SavedTAttr, TAttr: Termios;
   Cmd: Char;
begin
   Randomize;
   {
     TODO: customizable size of the field
       Keep in mind that OpenAt is recursive. So at certain Field size we may get a Stack Overflow.
   }
   FieldReset(MainField, 10, 10);

   if IsATTY(STDIN_FILENO) = 0 then
   begin
      WriteLn('ERROR: this is not a terminal!');
      Halt(1);
   end;
   {TODO: does not work on Windows}
   TCGetAttr(STDIN_FILENO, TAttr);
   TCGetAttr(STDIN_FILENO, SavedTAttr);
   TAttr.c_lflag := TAttr.c_lflag and (not (ICANON or ECHO));
   TAttr.c_cc[VMIN] := 1;
   TAttr.c_cc[VTIME] := 0;
   TCSetAttr(STDIN_FILENO, TCSAFLUSH, &tattr);

   FieldDisplay(MainField);

   while True do
   begin
      Read(Cmd);
      case Cmd of
         'w': MoveUp(MainField);
         's': MoveDown(MainField);
         'a': MoveLeft(MainField);
         'd': MoveRight(MainField);
         'f': FlagAtCursor(MainField);
         {TODO: restart the game on `r`}
         'q': break; {TODO: ask the user if they really want to exit. In case of accedental press of `q`}
         ' ': begin
                 {TODO: Victory condition (with a restart)}
                 if OpenAtCursor(MainField) then
                 begin
                    OpenAllBombs(MainField);
                    Write(Chr(27), '[', MainField.Rows,   'A');
                    Write(Chr(27), '[', MainField.Cols*3, 'D');
                    FieldDisplay(MainField);
                    {TODO: restart the game after death}
                    WriteLn('Oops!');
                    break;
                 end;
              end;
      end;
      Write(Chr(27), '[', MainField.Rows,   'A');
      Write(Chr(27), '[', MainField.Cols*3, 'D');
      FieldDisplay(MainField);
   end;

   TCSetAttr(STDIN_FILENO, TCSANOW, SavedTAttr);
end.
