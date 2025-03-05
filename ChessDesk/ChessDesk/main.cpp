#include <iostream>
#include <gl/glew.h>
#include <gl/freeglut.h>
#include <gl/freeglut_ext.h>

GLvoid drawScene();
GLvoid Reshape(int w, int h);
GLvoid keyboard(int key, int x, int y);

const int rows = 8, cols = 8;
float tileSize = 2.0f / rows;
float playerSize = tileSize * 0.6f;
int playerX = 0, playerY = 0;

void main(int argc, char** argv)
{
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowPosition(100, 100);
    glutInitWindowSize(800, 800);
    glutCreateWindow("Chess Board");

    glewExperimental = GL_TRUE;

    if (glewInit() != GLEW_OK)
    {
        std::cerr << "Your Chess Club BOOM" << std::endl;
        exit(EXIT_FAILURE);
    }

    else
    {
        std::cout << "Welcome To Chess Club\n";
    }

    glutDisplayFunc(drawScene);
    glutReshapeFunc(Reshape);
    glutSpecialFunc(keyboard); 
    glutMainLoop();
}

GLvoid drawScene()
{
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    for (int row = 0; row < rows; ++row)
    {
        for (int col = 0; col < cols; ++col)
        {
            if ((row + col) % 2 == 0)
            {
                glColor3f(1.0f, 1.0f, 1.0f);
            }

            else
            {
                glColor3f(0.0f, 0.0f, 0.0f);
            }

            float x = -1.0f + col * tileSize;
            float y = 1.0f - row * tileSize;

            glBegin(GL_QUADS);
            glVertex2f(x, y);
            glVertex2f(x + tileSize, y);
            glVertex2f(x + tileSize, y - tileSize);
            glVertex2f(x, y - tileSize);
            glEnd();
        }
    }

    glColor3f(0.5f, 0.5f, 0.5f);

    float px = -1.0f + playerX * tileSize + (tileSize - playerSize) / 2;
    float py = 1.0f - playerY * tileSize - (tileSize - playerSize) / 2;

    glBegin(GL_QUADS);
    glVertex2f(px, py);
    glVertex2f(px + playerSize, py);
    glVertex2f(px + playerSize, py - playerSize);
    glVertex2f(px, py - playerSize);
    glEnd();

    glutSwapBuffers();
}

GLvoid Reshape(int w, int h)
{
    glViewport(0, 0, w, h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluOrtho2D(-1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

GLvoid keyboard(int key, int x, int y)
{
    switch (key)
    {
    case GLUT_KEY_UP:
        if (playerY > 0)
        {
            playerY--;
        }
        break;

    case GLUT_KEY_DOWN:
        if (playerY < rows - 1)
        {
            playerY++;
        }
        break;

    case GLUT_KEY_LEFT:
        if (playerX > 0)
        {
            playerX--;
        }
        break;

    case GLUT_KEY_RIGHT:
        if (playerX < cols - 1)
        {
            playerX++;
        }
        break;
    }

    glutPostRedisplay();
}
