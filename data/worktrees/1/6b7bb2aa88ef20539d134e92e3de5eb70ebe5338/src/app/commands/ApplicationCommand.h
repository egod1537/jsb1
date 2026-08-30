class Application;

class ApplicationCommand {
public:
  virtual ~ApplicationCommand() = delete;
  virtual void Execute(Application &app) = 0;
};
