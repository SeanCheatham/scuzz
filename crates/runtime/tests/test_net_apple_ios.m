#import <UIKit/UIKit.h>
#include <pthread.h>
#include <stdlib.h>
extern int scuzz_net_apple_proof(void);
static void *run_proof(void *unused) {
  (void)unused;
  @autoreleasepool { exit(scuzz_net_apple_proof()); }
}
@interface NetProofDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end
@implementation NetProofDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
  (void)application; (void)options;
  self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
  self.window.rootViewController = [UIViewController new];
  [self.window makeKeyAndVisible];
  pthread_t thread;
  if (pthread_create(&thread, NULL, run_proof, NULL)) exit(1);
  pthread_detach(thread);
  return YES;
}
@end
int main(int argc, char **argv) {
  @autoreleasepool { return UIApplicationMain(argc, argv, nil, NSStringFromClass(NetProofDelegate.class)); }
}
