#include<iostream>
#include<vector>
#include<windows.h> // for sleep()
#include<stdlib.h>
#include<set>
#include<string>
#include<algorithm>
#include<chrono>
#include<cwchar>

using namespace std;

HANDLE hStdin;
HANDLE hStdout;

//higher precision than using sleep by itself, less CPU usage than a spinlock by itself (7% -> ~0%) (according to VS2019 profiler anyway)
void SpinSleep(int millis) {
    //Sleep(millis);
    auto t0 = chrono::high_resolution_clock::now();
    while (chrono::duration_cast<chrono::milliseconds>(chrono::high_resolution_clock::now() - t0).count() < millis) { 
        Sleep(1); 
    }
}

void resetCursor() {
    COORD topLeft = { 0, 0 };
    SetConsoleCursorPosition(hStdout, topLeft);
}

void clearConsole() {
    COORD topLeft = { 0, 0 };
    CONSOLE_SCREEN_BUFFER_INFO screen;
    DWORD written;

    GetConsoleScreenBufferInfo(hStdout, &screen);
    FillConsoleOutputCharacterA(
        hStdout, ' ', screen.dwSize.X * screen.dwSize.Y, topLeft, &written
    );
    FillConsoleOutputAttribute(
        hStdout, FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_BLUE,
        screen.dwSize.X * screen.dwSize.Y, topLeft, &written
    );

    resetCursor();
    
}



class Game {
    vector<vector<char>> screen;
    int w, h;

    void setChar(vector<vector<char>>& s, int x, int y, char c) {
        if (x < 0) return;
        if (y < 0) return;
        if (x >= screen[0].size()) return;
        if (y >= screen.size()) return;

        s[y][x] = c;
    }

    void setChar(int x, int y, char c) {
        setChar(screen, x, y, c);
    }

    vector<vector<char>> layout;

    bool checkSquare(int x, int y) {
        x -= 2;
        if (x < 0) return true;
        if (x >= layout[0].size()) return true;
        if (y < 0) return false; //false if there is no ceiling
        if (y >= layout.size()) return true;
        return (layout[y][x] != ' ');
    }

    vector<pair<int, int>> nextTetris;
    int nextTetrisX; 
    int nextTetrisY;
    char nextTetrisCol;
    bool generateNewTetris = true;

    int fps;
    
    int dim;

    chrono::time_point<chrono::steady_clock> t0;
    size_t time_elapsed;

    int gravityTimer;

    int height = 0;

    bool gameOver = false;

    size_t score = 0;
    //bool gameStart = false;

public:
    int dimensions() { return dim; }
    string timeTaken() { return to_string(time_elapsed / 1000) + ":" + to_string(time_elapsed % 1000); }
    size_t finalScore() { return score; }
    int finalLevel() { return difficulty(score); }



    Game(int w, int h, int dim): w(w), h(h), screen(h, vector<char>(w, ' ')), fps(30), dim(dim) {
        layout = vector<vector<char>>(h - 3, vector<char>(w - 4, ' '));

        nextTetris = vector<pair<int, int>>();

        t0 = chrono::high_resolution_clock::now();
        time_elapsed = 0;
        gravityTimer = 0;
    }

    void print() {
        for (int i = 0; i < screen.size(); i++) {
            for (char c : screen[i]) {
                cout << c;
            }
            cout << "\n";
        }
        cout << std::flush;
    }


    void clearChars(vector<vector<char>>& a) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                a[y][x] = ' ';
            }
        }
    }

    void text(string text, int x, int y) {
        for (int i = 0; i < text.size(); i++) {
            setChar(x + i, y, text[i]);
        }
    }

    void clearScreen() {
        clearChars(screen);
    }

    void handleInput(KEY_EVENT_RECORD e) {
        if (e.bKeyDown) {
            if (e.wVirtualKeyCode == VK_LEFT) {
                moveTetris(-1, 0);
            }
            else if (e.wVirtualKeyCode == VK_RIGHT) {
                moveTetris(1, 0);
            }
            else if (e.wVirtualKeyCode == VK_DOWN) {
                moveTetris(0, 1);
            }
            else if (e.wVirtualKeyCode == VK_UP) {
                rotateTetris();
            }
        }
        else {

        }
    }

    int mainLoop() {
        DWORD fdwSaveOldMode;
        if (!GetConsoleMode(hStdin, &fdwSaveOldMode))
            return -1;

        int lastNumRead = 0;
        DWORD cNumRead, fdwMode, i;
        INPUT_RECORD irInBuf[8];

        CONSOLE_FONT_INFOEX cfi;
        cfi.cbSize = sizeof(cfi);
        cfi.nFont = 0;
        cfi.dwFontSize.X = 24;                   // Width of each character in the font
        cfi.dwFontSize.Y = 24;                  // Height
        cfi.FontFamily = FF_DONTCARE;
        cfi.FontWeight = FW_NORMAL;
        wcscpy_s(cfi.FaceName, L"Courier new"); // Choose your font
        SetCurrentConsoleFontEx(hStdout, FALSE, &cfi);

        // Enable the window and mouse input events.
        fdwMode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
        if (!SetConsoleMode(hStdin, fdwMode)) {
            cout << "set console mode" << endl;
            return -1;
        }

        MoveWindow(GetConsoleWindow(), 20, 20, 24 * (w + 4), 24 * (h + 4), TRUE);

        while (!gameOver) {
            SpinSleep(1000/fps);
            clearScreen();

            if (PeekConsoleInput(hStdin, irInBuf, 8, &cNumRead)) {
                if(cNumRead>0)
                    ReadConsoleInput(hStdin, irInBuf, 8, &cNumRead);
            }
            else {
                cout << "input read failed" << endl;
                return -1;
            }

            for (i = 0; i < cNumRead; i++)
            {
                switch (irInBuf[i].EventType)
                {
                case KEY_EVENT: // keyboard input
                    handleInput(irInBuf[i].Event.KeyEvent);
                    break;
                default:
                    break;
                }
            }

            run();

            resetCursor();
            print();
        }

        // Restore input mode on exit.
        SetConsoleMode(hStdin, fdwSaveOldMode);

        return 0;
    }

    void run() {
        if (!gameOver) {
            auto t1 = chrono::high_resolution_clock::now() - t0;
            time_elapsed = chrono::duration_cast<chrono::milliseconds>(t1).count();

            if (generateNewTetris) {
                generateNewTetris = false;
                generateNextTetris();
            }
        }
        else {
            //Draw end screen
            nextTetris.clear();
        }

        drawWalls();
        drawLayout();
        drawNextTetris();
        drawStats();
    }

    bool isIntersecting(int xOffset, int yOffset) {
        //Perform collision detection before we move to check if its valid
        for (int i = 0; i < nextTetris.size(); i++) {
            int sX = nextTetris[i].first + nextTetrisX + xOffset;
            int sY = nextTetris[i].second + nextTetrisY + yOffset;

            if (checkSquare(sX, sY)) {
                return true;
            }
        }
        return false;
    }

    void moveTetris(int xDir, int yDir) {
        if (isIntersecting(xDir, yDir))
            return;

        nextTetrisX += xDir;
        nextTetrisY += yDir;
    }

    void drawStats() {
        string timeText = "time: " + to_string(time_elapsed/1000);
        text(timeText, 0, h-1);

        string scoreString = "score: " + to_string(score);
        text(scoreString, 0, h - 2);
        
        string lvString = "LV:" + to_string(difficulty(score));
        text(lvString, w - lvString.length(), h - 1);
    }

    void drawNextTetris() {
        //Move tetris down after some time
        //Make it move faster based on how much time has elapsed
        gravityTimer += 1;// + time_elapsed / 10000;
        if (gravityTimer > 30 - difficulty(score))
        {
            gravityTimer = 0;

            if(!bakeTetris())
                moveTetris(0, 1);
        }

        for (int i = 0; i < nextTetris.size(); i++) {
            int sX = nextTetris[i].first + nextTetrisX;
            int sY = nextTetris[i].second + nextTetrisY;

            setChar(sX, sY, nextTetrisCol);
        }
    }

    size_t difficulty(size_t score) {
        return 1 + log2(1 + score/500);
    }

    void clearRows() {
        int lower = -2;
        int upper= 2*h;

        //find the rows that our tetris would have influenced
        for (int i = 0; i < nextTetris.size(); i++) {
            int sY = nextTetris[i].second + nextTetrisY;
            if (sY < upper)
                upper = sY;
            if (sY > lower)
                lower = sY;
        }

        int rowsToClear = 0;

        //Scan area for rows
        for (int i = upper; i <= lower; i++) {
            bool row = true;
            for (int j = 0; j < layout[0].size(); j++) {
                if (layout[i][j] == ' ') {
                    row = false;
                    break;
                }
            }

            if (!row)
                continue;

            rowsToClear++;

            //mark row for demolishing
            //animate this in the screen as well
            for (int j = 0; j < layout[0].size(); j++) {
                layout[i][j] = '*';
                screen[i][j+2] = '*';
            }
        }

        if (rowsToClear==0)
            return;

        score += 500 * rowsToClear * rowsToClear * difficulty(score);
        //drawScore();
        resetCursor();
        print();
        SpinSleep(100);

        //Remove the rows by moving everything down
        for (int x = 0; x < layout[0].size(); x++) {
            int backwardsOffset = 0;
            for (int y = layout.size()-1; y >= 0; y--) {
                if (layout[y][x] == '*') {
                    backwardsOffset++;
                }
                else {
                    //Not a typical swap, this order is deliberate
                    char temp = layout[y][x];
                    layout[y][x] = ' ';
                    layout[y+backwardsOffset][x] = temp;
                }
            }
        }
    }

    bool bakeTetris() {
        bool canBake = isIntersecting(0,1);
        //check if this tetris is freezeable

        if (canBake) {
            //check if this tetris is freezeable
            for (int i = 0; i < nextTetris.size(); i++) {
                int sX = nextTetris[i].first + nextTetrisX;
                int sY = nextTetris[i].second + nextTetrisY;

                setChar(layout, sX-2, sY, nextTetrisCol);
                
                int newHeight = h - sY;
                if (newHeight > height) {
                    height = newHeight;
                    if (height >= h)
                        gameOver = true;
                }
            }

            generateNewTetris = true;

            clearRows();

            return true;
        }
        return false;
    }

    void drawWalls() {
        //vertical walls
        for (int i = 0; i < screen.size(); i++) {
            screen[i][0] = '|';
            screen[i][1] = '|';

            screen[i][w - 1] = '|';
            screen[i][w - 2] = '|';
        }

        for (int i = 0; i < screen[0].size(); i++) {
            screen[h - 3][i] = 'I';
            //screen[h - 2][i] = 'I';
            //screen[h - 1][i] = 'I';
        }
    }

    void drawLayout() {
        for (int i = 0; i < layout.size(); i++) {
            for (int j = 0; j < layout[0].size(); j++) {
                screen[i][j + 2] = layout[i][j];
            }
        }
    }

    pair<int, int> p(int x, int y) {
        return pair<int, int>(x, y);
    }

    //procedurally generates the next tetris (or n-ris rather(idk if thats what its called))
    void generateNextTetris() {
        nextTetris.clear();

        nextTetrisCol = "rgbyop"[rand() % 6];

        set<pair<int, int>> area;
        set<pair<int, int>> perimiter;

        int nextX = 0;
        int nextY = 0;

        //generate blocks on top of our initial one using a random breadth first search
        for (int i = 0; i < dim; i++) {
            //So that 0,0 gets inserted
            area.insert(p(nextX, nextY));
            nextTetris.push_back(p(nextX, nextY));

            for (int x = -1; x < 2; x++) {
                for (int y = -1; y < 2; y ++) {
                    if (!(x == 0 || y == 0))
                        continue;

                    pair<int, int> nextPerimiter = p(nextX + x, nextY + y);
                    if (area.count(nextPerimiter)==0) {
                        perimiter.insert(nextPerimiter);
                    }
                }
            }

            int next = rand() % perimiter.size();
            auto it = perimiter.begin();
            advance(it, next);
            pair<int, int> nextP = *it;
            
            nextX = nextP.first;
            nextY = nextP.second;
            perimiter.erase(it);
        }
        
        //now let us find the center of mass and then reposition the tetris
        pair<int, int> centerOfMass = p(0, 0);

        for (int i = 0; i < nextTetris.size(); i++) {
            centerOfMass.first += nextTetris[i].first;
            centerOfMass.second += nextTetris[i].second;
        }

        //the cast is important. otherwise, -1/unsinged 4 becoimes  UINT_MAX/4 unsinged division
        centerOfMass.first /= (int) nextTetris.size();
        centerOfMass.second /= (int) nextTetris.size();
        
        for (int i = 0; i < nextTetris.size(); i++) {
            nextTetris[i].first -= centerOfMass.first;
            nextTetris[i].second -= centerOfMass.second;
        }
        

        nextTetrisX = w/2;
        nextTetrisY = 0;

        for (int i = 0; i < nextTetris.size(); i++) {
            if (nextTetris[i].second + nextTetrisY < 0) {
                nextTetrisY = -nextTetris[i].second;
            }
        }
    }

    void rotateTetris() {
        //Perform collision detection before we rotate to check if its valid
        for (int i = 0; i < nextTetris.size(); i++) {
            int sX = nextTetrisX + nextTetris[i].second;
            int sY = nextTetrisY  -nextTetris[i].first;

            if (checkSquare(sX, sY)) {
                return;
            }
        }

        for (int i = 0; i < nextTetris.size(); i++) {
            int x = nextTetris[i].second;
            int y = -nextTetris[i].first;
            nextTetris[i].first = x;
            nextTetris[i].second = y;
        }
    }
};


Game startGame() {
    string input;
    int d = 0;

    cout << "N-ris is a tetris clone in the command line. Use the arrow keys to move and rotate the block.\n";
    while (d < 1 || d > 10) {
        cout << "How many blocks must your n-rominoes have? (input a value between 1 and 10. normal tetris would be n=4)" << endl;
        cin >> input;
        try {
            d = stoi(input);
        }
        catch (exception e) {
            cout << "That was an invalid input. try again - " << endl;
            continue;
        }

        if (d == 1) {
            cout << "At that point you are stacking 1x1 squares together. are you sure about this? (y/n)" << endl;
        }
        else if (d > 45) {
            cout << "your screen is too small for such a large game." << endl;
            continue;
        }
        else if (d > 10) {
            cout << "This game may not work for such high values of n. continue anyway? (y/n)" << endl;
        }
        else if (d < 1) {
            cout << "0 and negative numbers dont work" << endl;
            continue;
        }
        else {
            continue;
        }

        cin >> input;

        if (input[0] == 'y' || input[0] == 'Y') {
            break;
        }
    }

    return Game(10 + d, 16 + d, d);
}

int main() {
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    hStdout = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD fdwSaveOldMode;
    int windowX, windowY, width, height;

    if (!GetConsoleMode(hStdin, &fdwSaveOldMode))
        return -1;

    RECT windowRect;
    if (!GetWindowRect(GetConsoleWindow(), &windowRect)) {
        cout << "Couldnt get window rect" << endl;
        cout << GetLastError() << endl;
    }

    CONSOLE_FONT_INFOEX initFontInfo;
    initFontInfo.cbSize = sizeof(initFontInfo);

    if (!GetCurrentConsoleFontEx(hStdout, FALSE, &initFontInfo)) {
        cout << "couldnt get font" << endl;
        cout << GetLastError() << endl;
    }

    
    string input;
    while (true) {
        Game g = startGame();

        clearConsole();
        g.mainLoop();

        SetConsoleMode(hStdin, fdwSaveOldMode);
        if (!SetCurrentConsoleFontEx(hStdout, FALSE, &initFontInfo)) 
        {
           cout << GetLastError() << endl;
        }

        MoveWindow(GetConsoleWindow(), windowRect.left, windowRect.top, windowRect.right-windowRect.left, windowRect.bottom-windowRect.top, TRUE);


        cout << "\n\n\n";
        cout << "\t------------------------------------\n";
        cout << "\t---------------RESULTS--------------\n";
        cout << "\t------------------------------------\n";
        cout << "\tN-Ris N = " << g.dimensions() <<"\n";
        cout << "\t------------------------------------\n";
        cout << "\tTotal Time: " << g.timeTaken() << " \n";
        cout << "\t------------------------------------\n";
        cout << "\tTotal Score: " << g.finalScore() << "\n";
        cout << "\t------------------------------------\n";
        cout << "\tFinal level: " << g.finalLevel() << "\n";
        cout << "\t------------------------------------\n";
        cout << "\n";
        cout << "Would you like to play again?(y/n)" << endl;
        cin >> input;

        if (!(input[0] == 'y' || input[0] == 'Y')) {
            break;
        }
    }

    cout << "\nSee you next time! \n(you are now obliged to play again else a worm that was inserted into \nyour computer while you were playing will encrypt all your files)\n(jk)\n(unless...\nO_O)" << endl;

    SpinSleep(3000);
    SetConsoleMode(hStdin, fdwSaveOldMode);
    return 0;
}